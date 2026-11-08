// Kijelzőegység – műszerfal a valós telemetriával (S-07, S-08, S-10 első változata).
//
// Mit csinál: az ESP-NOW-n érkező adatokat három LVGL-képernyőn mutatja, amelyek 5 s-onként
//             animálva váltanak (érintővel balra/jobbra húzva is): (1) fő műszer: fordulatszám-ív, fordulatszám és sebesség;
//             (2) részletek: hűtővíz, fojtószelep, feszültség, MIL és hibakódok; (3) kapcsolat:
//             csomag/s, veszteség, térerő, sorszám, az adat kora. 500 ms-nál régebbi adatnál
//             "NINCS ADAT" jelzés. Másodpercenként egy naplósor: kapcsolatstatisztika és FPS.
//             Másodpercenként életjelet küld az OBD-egységnek, megméri az akkumulátort (a
//             százalék a fő képernyőn, a feszültség a részletek között látszik), és frissíti a
//             LED-ek állapotát.
// Hogyan:     két feladat két magon. A 0. magon a kapcsolatfeladat fogadja a csomagokat és
//             frissíti a DashModel-t (ugyanitt fut a Wi-Fi-feladat is); az 1. magon az LVGL egy
//             16 ms-os időzítővel kiolvassa a modellt és frissíti a képernyőket. A modellt mutex
//             védi; a zár csak a másolás idejére tart, a rajzolás már a másolaton dolgozik.
// Miért:      így a rádió és a rajzolás nem lassítja egymást, és a megjelenítés logikája (mi
//             friss, mi "nincs adat") a PC-n tesztelt DashModel-ben van, nem a nézetekben.
//
// A beépített Montserrat betűkészletekben nincsenek ékezetes betűk, ezért a feliratok egyelőre
// ékezet nélküli nagybetűsek; a magyar karakterkészletű betűtípus külön lépés.
#include "dash_app.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "battery_adc.h"
#include "battery_gauge.h"
#include "ble_ams.h"
#include "ble_radio.h"
#include "board_pins.h"
#include "dash_model.h"
#include "display_power.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_pattern.h"
#include "music_model.h"
#include "og_fonts.h"
#include "og_logo.h"
#include "og_vin.h"
#include "settings.h"
#include "settings_store.h"
#include "settings_view.h"
#include "lvgl_setup.h"
#include "og_codec.h"
#include "sdkconfig.h"

namespace og {

namespace {

const char* TAG = "og_dash";

constexpr uint32_t kUiPeriodMs = 16;       // a nézetek frissítése (~60 Hz)
constexpr uint32_t kSwitchAnimMs = 400;
constexpr uint32_t kTouchPauseMs = 15000;  // érintés után ennyi ideig nincs automatikus váltás
constexpr uint32_t kLogoFadeInMs = 1000;   // az indító logó felúszása
constexpr uint32_t kLogoHoldMs = 2000;     // a logó teljes láthatósága
constexpr uint32_t kLogoFadeOutMs = 1000;  // áttűnés a műszerfalra
constexpr uint32_t kRadioFaultMs = 3000;      // ennyi csend után rádióhiba (LED-táblázat)
constexpr uint32_t kPowerFaultMs = 5000;      // alacsony feszültség ennyi ideig folyamatosan
constexpr uint32_t kFaultClearMs = 1000;
constexpr int kLowBatteryBacklight = 40;      // alacsony akkumulátornál a háttérvilágítás felső korlátja (%)
constexpr uint32_t kBatteryLogPeriodS = 10;

std::mutex g_mx;      // a g_model-t és a g_battery-t védi (0. mag: írás, 1. mag: olvasás)
DashModel g_model;
BatteryStatus g_battery;
EspNowLink g_link;
StatusLed* g_led = nullptr;
LcdPanel* g_lcd = nullptr;
BatteryAdc g_adc;
BatteryGauge g_gauge;
bool g_adc_ok = false;
int g_backlight_pct = CONFIG_OG_LCD_BACKLIGHT_PERCENT;  // a jelenlegi (akkumulátorszinttől függő) fényerő
std::atomic<int> g_sleep_in_s{-1};                      // az alvásig hátralévő idő a kijelzőre (-1: nem mutatjuk)
#if defined(CONFIG_OG_DISP_SLEEP)
DisplayPowerFsm g_power(DisplayPowerConfig{CONFIG_OG_DISP_SLEEP_AFTER_S * 1000u, CONFIG_OG_DISP_SLEEP_GRACE_S * 1000u});
#endif

inline uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Mit csinál: tizedesvesszős számot ír (a magyar írásmód szerint).
void fmt_dec(char* out, size_t cap, const char* fmt, double v) {
    std::snprintf(out, cap, fmt, v);
    for (char* p = out; *p; ++p)
        if (*p == '.') *p = ',';
}

// Mit csinál: csak akkor írja át a feliratot, ha a szöveg változott.
// Miért:      a változatlan felirat újraírása is újrarajzolást kérne (fölösleges CPU-idő).
void set_text(lv_obj_t* label, const char* text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// A kiválasztott paletta színei; az apply_palette() tölti fel, még a képernyők építése előtt.
lv_color_t g_bg, g_track, g_text, g_dim, g_accent, g_warn;

lv_obj_t* make_screen() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, g_bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);  // a húzás gesztusként érkezzen, ne görgetésként
    return scr;
}

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, lv_align_t align, int32_t x,
                     int32_t y, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    lv_obj_align(l, align, x, y);
    return l;
}

Settings g_cfg;  // a betöltött beállítások (indításkor állnak be, utána csak olvassuk)

// Mit csinál: a beállításban kiválasztott palettát LVGL-színekké alakítja.
void apply_palette(const Settings& s) {
    const Palette& p = palette_get(s.palette);
    g_bg = lv_color_hex(p.bg);
    g_track = lv_color_hex(p.track);
    g_text = lv_color_hex(p.text);
    g_dim = lv_color_hex(p.dim);
    g_accent = lv_color_hex(p.accent);
    g_warn = lv_color_hex(p.warn);
}

// Mit csinál: a töltöttséghez illő akkumulátor-ikon (töltés becsült állapotában villám).
const char* battery_symbol(const BatteryStatus& b) {
    if (b.charging) return LV_SYMBOL_CHARGE;
    if (b.percent >= 88) return LV_SYMBOL_BATTERY_FULL;
    if (b.percent >= 63) return LV_SYMBOL_BATTERY_3;
    if (b.percent >= 38) return LV_SYMBOL_BATTERY_2;
    if (b.percent >= 13) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

inline bool battery_shown(const BatteryStatus& b) { return b.valid && b.level != BatteryLevel::Absent; }

// ------------------------------------------------------------------------------- nézetek

// (1) Fő műszer: 270°-os fordulatszám-ív, középen a fordulatszám, alatta a sebesség.
struct MainView {
    lv_obj_t* scr;
    lv_obj_t* arc;
    lv_obj_t* rpm;
    lv_obj_t* speed;
    lv_obj_t* nodata;
    lv_obj_t* battery;
    lv_obj_t* sleepmsg;
    bool red = false;
    bool battery_red = false;
    int32_t rpm_max = 8000;  // a skála vége és a piros sáv kezdete (a beállításokból)
    int32_t rpm_red = 6000;

    // Mit csinál: új skálavég és redline (beállítás módosításakor).
    void set_scale(int32_t max, int32_t redline) {
        rpm_max = max;
        rpm_red = redline;
        lv_arc_set_range(arc, 0, rpm_max);
    }

    void build() {
        scr = make_screen();
        arc = lv_arc_create(scr);
        lv_obj_set_size(arc, 320, 320);
        lv_obj_center(arc);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_range(arc, 0, rpm_max);
        lv_arc_set_value(arc, 0);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_set_clickable(arc, false);
        lv_obj_set_style_arc_width(arc, 18, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 18, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, g_track, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, g_accent, LV_PART_INDICATOR);

        rpm = make_label(scr, &lv_font_montserrat_48, g_text, LV_ALIGN_CENTER, 0, -30, "--");
        make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, 0, 10, "1/perc");
        speed = make_label(scr, &og_font_montserrat_28, g_text, LV_ALIGN_CENTER, 0, 60, "-- km/h");
        nodata = make_label(scr, &og_font_montserrat_20, g_warn, LV_ALIGN_CENTER, 0, 110, "NINCS ADAT");
        battery = make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_TOP_MID, 0, 62, "");
        sleepmsg = make_label(scr, &og_font_montserrat_20, g_accent, LV_ALIGN_BOTTOM_MID, 0, -26, "");
    }

    void update(const DashValues& v, const BatteryStatus& bat, int sleep_in_s) {
        char b[24];
        if (sleep_in_s >= 0) std::snprintf(b, sizeof b, "ALVÁS %d S", sleep_in_s);
        else b[0] = 0;
        set_text(sleepmsg, b);
        if (battery_shown(bat)) std::snprintf(b, sizeof b, "%s %u %%", battery_symbol(bat), bat.percent);
        else b[0] = 0;
        set_text(battery, b);
        const bool bat_red = battery_shown(bat) && bat.level != BatteryLevel::Ok;
        if (bat_red != battery_red) {
            lv_obj_set_style_text_color(battery, bat_red ? g_warn : g_dim, 0);
            battery_red = bat_red;
        }
        const bool ok_rpm = v.fresh && (v.flags & kFlagRpm);
        const int32_t r = ok_rpm ? static_cast<int32_t>(v.rpm + 0.5f) : 0;
        if (lv_arc_get_value(arc) != r) lv_arc_set_value(arc, r);
        const bool want_red = r >= rpm_red;
        if (want_red != red) {
            lv_obj_set_style_arc_color(arc, want_red ? g_warn : g_accent, LV_PART_INDICATOR);
            red = want_red;
        }
        if (ok_rpm)
            std::snprintf(b, sizeof b, "%ld", static_cast<long>(r / 10 * 10));  // 10-es lépés: nyugodtabb szám
        else
            std::snprintf(b, sizeof b, "--");
        set_text(rpm, b);
        const bool mph = g_cfg.speed_unit == SpeedUnit::Mph;
        if (v.fresh && (v.flags & kFlagSpeed))
            std::snprintf(b, sizeof b, mph ? "%d mph" : "%d km/h", static_cast<int>((mph ? v.speed_kmh * 0.621371f : v.speed_kmh) + 0.5f));
        else
            std::snprintf(b, sizeof b, mph ? "-- mph" : "-- km/h");
        set_text(speed, b);
        if (lv_obj_is_hidden(nodata) != v.fresh) lv_obj_set_hidden(nodata, v.fresh);
    }
};

// Egy "név – érték" sor a részletes és a kapcsolat nézethez.
struct Row {
    lv_obj_t* value;
    void build(lv_obj_t* scr, int32_t y, const char* name) {
        make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, -70, y, name);
        value = make_label(scr, &og_font_montserrat_28, g_text, LV_ALIGN_CENTER, 65, y, "--");
    }
};

// (2) Részletek: hűtővíz, fojtószelep, a jármű feszültsége, MIL és hibakódok, a cella feszültsége.
struct DetailView {
    lv_obj_t* scr;
    Row coolant, throttle, vbat, mil, cell;
    lv_obj_t* vin;
    bool mil_red = false;

    void build() {
        scr = make_screen();
        make_label(scr, &og_font_montserrat_20, g_accent, LV_ALIGN_TOP_MID, 0, 45, "RÉSZLETEK");
        coolant.build(scr, -90, "VIZ");
        throttle.build(scr, -45, "FOJTO");
        vbat.build(scr, 0, "JARMU");
        mil.build(scr, 45, "MIL");
        cell.build(scr, 90, "CELLA");
        vin = make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, 0, 124, "");
    }

    void update(const DashValues& v, const BatteryStatus& bat, const char* vin_text) {
        set_text(vin, vin_text);
        char b[24];
        if (battery_shown(bat)) fmt_dec(b, sizeof b, "%.2f V", bat.mv / 1000.0);
        else std::snprintf(b, sizeof b, "--");
        set_text(cell.value, b);
        auto ok = [&](uint16_t f) { return v.fresh && (v.flags & f); };
        const bool fahrenheit = g_cfg.temp_unit == TempUnit::Fahrenheit;
        if (ok(kFlagCoolant))
            std::snprintf(b, sizeof b, fahrenheit ? "%d °F" : "%d °C",
                          static_cast<int>(fahrenheit ? v.coolant_c * 1.8f + 32.f : v.coolant_c));
        else std::snprintf(b, sizeof b, "--");
        set_text(coolant.value, b);
        if (ok(kFlagThrottle)) std::snprintf(b, sizeof b, "%d %%", static_cast<int>(v.throttle_pct + 0.5f));
        else std::snprintf(b, sizeof b, "--");
        set_text(throttle.value, b);
        if (ok(kFlagVbat)) fmt_dec(b, sizeof b, "%.1f V", v.vbat_v);
        else std::snprintf(b, sizeof b, "--");
        set_text(vbat.value, b);
        if (ok(kFlagMil)) std::snprintf(b, sizeof b, v.mil_on ? "BE (%u)" : "KI (%u)", v.dtc_count);
        else std::snprintf(b, sizeof b, "--");
        set_text(mil.value, b);
        const bool want_red = ok(kFlagMil) && v.mil_on;
        if (want_red != mil_red) {  // stílust csak változáskor: különben minden ütemben újrarajzolna
            lv_obj_set_style_text_color(mil.value, want_red ? g_warn : g_text, 0);
            mil_red = want_red;
        }
    }
};

// (3) Kapcsolat: csomag/s, veszteség, térerő, utolsó sorszám, az adat kora.
struct LinkView {
    lv_obj_t* scr;
    Row rate, loss, rssi, seq, age;
    bool age_red = false;

    void build() {
        scr = make_screen();
        make_label(scr, &og_font_montserrat_20, g_accent, LV_ALIGN_TOP_MID, 0, 45, "KAPCSOLAT");
        rate.build(scr, -90, "CSOMAG/S");
        loss.build(scr, -45, "VESZTES");
        rssi.build(scr, 0, "RSSI");
        seq.build(scr, 45, "SORSZAM");
        age.build(scr, 90, "KOR");
    }

    void update(const DashValues& v, const DashLink& l) {
        char b[24];
        std::snprintf(b, sizeof b, "%lu", static_cast<unsigned long>(l.per_sec));
        set_text(rate.value, b);
        fmt_dec(b, sizeof b, "%.2f %%", l.loss_ratio * 100.0);
        set_text(loss.value, b);
        if (l.per_sec) std::snprintf(b, sizeof b, "%d dBm", l.rssi_dbm);
        else std::snprintf(b, sizeof b, "--");
        set_text(rssi.value, b);
        std::snprintf(b, sizeof b, "%u", l.last_seq);
        set_text(seq.value, b);
        if (!v.has_data) std::snprintf(b, sizeof b, "--");
        else if (v.age_ms < 10000) std::snprintf(b, sizeof b, "%lu ms", static_cast<unsigned long>(v.age_ms));
        else std::snprintf(b, sizeof b, "%lu s", static_cast<unsigned long>(v.age_ms / 1000));
        set_text(age.value, b);
        if (age_red == v.fresh) {
            lv_obj_set_style_text_color(age.value, v.fresh ? g_text : g_warn, 0);
            age_red = !v.fresh;
        }
    }
};

#if defined(CONFIG_OG_DISP_MUSIC)
// Mit csinál: a képernyő gombjai (előző, lejátszás/szünet, következő) lejátszásvezérlő parancsot küldenek az iPhone-nak.
void on_music_button(lv_event_t* e) {
    const auto cmd = static_cast<AmsCommand>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    ble_ams_command(cmd);
}

// (4) Zene: az iPhone "Most játszik" adatai (AMS): cím, előadó, folyamatjelző, idő, vezérlőgombok.
// Hogyan:     a hosszú cím/előadó körbeforgó görgetéssel fér ki; az ékezeteket az ASCII-ra egyszerűsítés
//             (music_model.h) cseréli, mert a betűkészletben nincsenek; az eltelt időt a nézet számolja a
//             lejátszási sebességgel, mert az iPhone csak változáskor küld új értéket.
struct MusicView {
    lv_obj_t* scr;
    lv_obj_t *title, *artist, *bar, *time, *play_icon;
    int32_t last_bar = -1;

    // Mit csinál: egy kerek gombot rak a képernyő aljára a megadott szimbólummal és paranccsal; a gomb feliratát adja vissza.
    lv_obj_t* add_button(int32_t x, const char* symbol, AmsCommand cmd) {
        lv_obj_t* b = lv_button_create(scr);
        lv_obj_set_size(b, 60, 60);
        lv_obj_align(b, LV_ALIGN_CENTER, x, 108);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, g_track, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, on_music_button, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(cmd)));
        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &og_font_montserrat_28, 0);
        lv_label_set_text(l, symbol);
        lv_obj_center(l);
        return l;
    }

    void build() {
        scr = make_screen();
        make_label(scr, &og_font_montserrat_20, g_accent, LV_ALIGN_TOP_MID, 0, 45, "ZENE");
        title = make_label(scr, &og_font_montserrat_28, g_text, LV_ALIGN_CENTER, 0, -60, "");
        artist = make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, 0, -22, "");
        for (lv_obj_t* l : {title, artist}) {
            lv_obj_set_width(l, 250);
            lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        }
        bar = lv_bar_create(scr);
        lv_obj_set_size(bar, 220, 8);
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, 22);
        lv_bar_set_range(bar, 0, 1000);
        lv_obj_set_style_bg_color(bar, g_track, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, g_accent, LV_PART_INDICATOR);
        time = make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, 0, 48, "");
        add_button(-85, LV_SYMBOL_PREV, AmsCommand::PreviousTrack);
        play_icon = add_button(0, LV_SYMBOL_PLAY, AmsCommand::TogglePlayPause);
        add_button(85, LV_SYMBOL_NEXT, AmsCommand::NextTrack);
    }

    // Mit csinál: a megjelenített adat frissítése az AMS pillanatképéből (az LVGL-feladatban hívva).
    void update(const AmsInfo& i, uint32_t now) {
        char t[sizeof i.title], a[sizeof i.artist], tm[40];
        const bool playing = i.playback == 1 || i.playback == 2 || i.playback == 3;
        if (!i.connected) {
            std::snprintf(t, sizeof t, "NINCS TELEFON");
            std::snprintf(a, sizeof a, "Keresd: OpenGauge");
        } else if (!i.ready) {
            std::snprintf(t, sizeof t, "PÁROSÍTÁS...");
            a[0] = 0;
        } else if (!i.title[0]) {
            std::snprintf(t, sizeof t, "NINCS ZENE");
            sanitize_text(i.player, a, sizeof a);
        } else {
            sanitize_text(i.title, t, sizeof t);
            sanitize_text(i.artist, a, sizeof a);
        }
        set_text(title, t);
        set_text(artist, a);
        const float e = music_elapsed(i.elapsed_s, i.playback_stamp_ms, i.rate, playing, now, i.duration_s);
        if (i.ready && i.title[0]) {
            char es[16], ds[16];
            format_time(es, sizeof es, e);
            if (i.duration_s > 0) {
                format_time(ds, sizeof ds, i.duration_s);
                std::snprintf(tm, sizeof tm, "%s / %s", es, ds);
            } else {
                std::snprintf(tm, sizeof tm, "%s", es);
            }
        } else {
            tm[0] = 0;
        }
        set_text(time, tm);
        const int32_t pm = static_cast<int32_t>(music_progress(e, i.duration_s) * 1000);
        if (pm != last_bar) {
            lv_bar_set_value(bar, pm, LV_ANIM_OFF);
            last_bar = pm;
        }
        set_text(play_icon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
};
MusicView g_music;
#endif
constexpr int kMusicId = 3;  // a zene képernyőre csak kézzel lépünk; az automatikus váltás kihagyja

MainView g_main;
DetailView g_detail;
LinkView g_linkv;
lv_obj_t* g_screens[kScreenIds] = {};  // képernyőazonosító szerint: 0 fő, 1 részletek, 2 kapcsolat, 3 zene
int g_active[kScreenIds];               // a lapozásban részt vevő képernyők, a beállított sorrendben
int g_active_n = 0;
int g_pos = 0;                          // a jelenlegi képernyő helye a g_active-ban
lv_timer_t* g_switch_timer = nullptr;
uint32_t g_pause_until = 0;
SettingsView g_settings_view;
lv_obj_t* g_cog = nullptr;              // a beállítások ikonja (felfelé húzásra jelenik meg)
lv_timer_t* g_cog_timer = nullptr;
bool g_dimmed = false;                  // alacsony akkumulátor miatt vett vissza a fényerőből
constexpr uint32_t kCogShowMs = 4000;

// Mit csinál: kiolvassa a modellt (rövid zár alatt), és frissíti mindhárom nézetet.
// Hogyan:     LVGL-időzítő, az LVGL-feladatban fut (az LVGL zárja már nála van).
// Miért:      a nem látható képernyők frissítése nem jár rajzolással, váltáskor viszont már
//             a friss értékek látszanak.
void ui_tick(lv_timer_t*) {
    DashValues v;
    DashLink l;
    BatteryStatus bat;
    char vin[18];
    {
        std::lock_guard<std::mutex> lock(g_mx);
        v = g_model.values(now_ms());
        l = g_model.link();
        bat = g_battery;
        std::memcpy(vin, g_model.vin(), sizeof vin);
    }
    g_main.update(v, bat, g_sleep_in_s.load(std::memory_order_relaxed));
    g_detail.update(v, bat, vin);
    g_linkv.update(v, l);
#if defined(CONFIG_OG_DISP_MUSIC)
    g_music.update(ble_ams_info(), now_ms());
#endif
}

// Mit csinál: megmondja, részt vesz-e a képernyő a lapozásban a beállítások szerint.
// Miért:      a Részletek és a Kapcsolat a debug módhoz tartozik, a zene a zene-kapcsolóhoz.
bool screen_enabled(int id) {
    if (!g_screens[id]) return false;
    if (id == 1 || id == 2) return g_cfg.debug_mode;
    if (id == kMusicId) return g_cfg.music_enabled;
    return true;
}

// Mit csinál: újraépíti a lapozási listát a beállított sorrend és a kapcsolók szerint; a jelenlegi
//             képernyő helyét megtartja, ha az még szerepel a listában.
void rebuild_active() {
    const int cur_id = g_active_n ? g_active[g_pos] : 0;
    g_active_n = 0;
    for (int i = 0; i < kScreenIds; ++i)
        if (screen_enabled(g_cfg.screen_order[i])) g_active[g_active_n++] = g_cfg.screen_order[i];
    if (g_active_n == 0) g_active[g_active_n++] = 0;
    g_pos = 0;
    for (int k = 0; k < g_active_n; ++k)
        if (g_active[k] == cur_id) g_pos = k;
}

void load_pos(int pos, lv_screen_load_anim_t anim) {
    g_pos = pos;
    lv_screen_load_anim(g_screens[g_active[g_pos]], anim, kSwitchAnimMs, 0, false);
}

// Mit csinál: a szomszédos képernyőre vált (step = +1: következő, -1: előző), csúszó animációval.
void show_next(int step) {
    load_pos((g_pos + step + g_active_n) % g_active_n, step > 0 ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

// Mit csinál: automatikus váltás, ha az utolsó érintés óta eltelt kTouchPauseMs, és be van kapcsolva.
//             A zene képernyőt kihagyja, és arról magától nem lép tovább; a beállítások alatt sem vált.
void switch_tick(lv_timer_t*) {
    if (!g_cfg.auto_switch || static_cast<int32_t>(now_ms() - g_pause_until) < 0) return;
    if (lv_screen_active() != g_screens[g_active[g_pos]]) return;  // logó, beállítások vagy váltás közben
    if (g_active[g_pos] == kMusicId) return;
    for (int k = 1; k < g_active_n; ++k) {
        const int p = (g_pos + k) % g_active_n;
        if (g_active[p] != kMusicId) {
            load_pos(p, LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
            return;
        }
    }
}

void set_perf_visible(bool on) {
    if (on) lv_sysmon_show_performance(lv_display_get_default());
    else lv_sysmon_hide_performance(lv_display_get_default());
}

// Mit csinál: elrejti a beállítások ikonját, és visszaadja a helyét az FPS/CPU kijelzésnek (debug módban).
void hide_cog() {
    if (g_cog_timer) {
        lv_timer_delete(g_cog_timer);
        g_cog_timer = nullptr;
    }
    if (g_cog && !lv_obj_has_flag(g_cog, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(g_cog, LV_OBJ_FLAG_HIDDEN);
        if (g_cfg.debug_mode) set_perf_visible(true);
    }
}

void cog_timeout(lv_timer_t*) {
    g_cog_timer = nullptr;  // az egyszeri időzítő magát törli
    hide_cog();
}

// Mit csinál: felfelé húzásra felmutatja a beállítások ikonját az FPS/CPU kijelzés helyén, kCogShowMs-ig.
void show_cog() {
    set_perf_visible(false);
    lv_obj_remove_flag(g_cog, LV_OBJ_FLAG_HIDDEN);
    if (g_cog_timer) lv_timer_delete(g_cog_timer);
    g_cog_timer = lv_timer_create(cog_timeout, kCogShowMs, nullptr);
    lv_timer_set_repeat_count(g_cog_timer, 1);
    g_pause_until = now_ms() + kTouchPauseMs;
}

// Mit csinál: az ikonra koppintva megnyitja a beállítások képernyőjét.
void open_settings(lv_event_t*) {
    hide_cog();
    g_settings_view.open(g_cfg);
    lv_screen_load_anim(g_settings_view.scr, LV_SCREEN_LOAD_ANIM_MOVE_TOP, kSwitchAnimMs, 0, false);
}

// Mit csinál: a módosított beállításokat azonnal érvényesíti (skála, fényerő, lapozás, debug kijelzés, lista).
void apply_live() {
    g_main.set_scale(g_cfg.max_rpm, g_cfg.redline_rpm);
    if (g_switch_timer) lv_timer_set_period(g_switch_timer, g_cfg.switch_period_s * 1000u);
    g_backlight_pct = g_dimmed ? std::min<int>(g_cfg.brightness_pct, kLowBatteryBacklight) : g_cfg.brightness_pct;
    if (g_lcd) g_lcd->set_backlight(g_backlight_pct);
    set_perf_visible(g_cfg.debug_mode);
    rebuild_active();
}

// Mit csinál: a beállítások képernyőjéről kilépve elmenti az értékeket és érvényesíti őket.
// Miért:      a paletta és a zene (BLE) csak újraindítással cserélhető: az előbbi a képernyők építésekor
//             áll be, az utóbbi a BLE-indítást érinti; ezért ezek változásakor a kijelző újraindul.
void on_settings_exit(const Settings& s) {
    const bool restart = s.palette != g_cfg.palette || s.music_enabled != g_cfg.music_enabled;
    g_cfg = s;
    if (!settings_save(g_cfg)) ESP_LOGW(TAG, "A beállítások mentése nem sikerült");
    if (restart) {
        ESP_LOGI(TAG, "A beállítás újraindítást kér");
        esp_restart();
    }
    apply_live();
    g_pause_until = now_ms() + kTouchPauseMs;
    lv_screen_load_anim(g_screens[g_active[g_pos]], LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, kSwitchAnimMs, 0, false);
}

// Mit csinál: balra húzás: következő képernyő, jobbra húzás: előző, felfelé húzás: beállítások ikonja;
//             utána szünetel az automatikus váltás, hogy a kézzel választott képernyő ne tűnjön el azonnal.
void on_gesture(lv_event_t*) {
    lv_indev_t* in = lv_indev_active();
    if (!in) return;
    const lv_dir_t d = lv_indev_get_gesture_dir(in);
    if (d == LV_DIR_TOP) {
        show_cog();
        return;
    }
    if (d != LV_DIR_LEFT && d != LV_DIR_RIGHT) return;
    hide_cog();
    show_next(d == LV_DIR_LEFT ? +1 : -1);
    g_pause_until = now_ms() + kTouchPauseMs;
    lv_timer_reset(g_switch_timer);
}

// ---------------------------------------------------------------------- kapcsolatfeladat

// Mit csinál: megméri az akkumulátort, alacsony szintnél csökkenti a háttérvilágítást, és
//             a tartósan alacsony feszültséget hibaként jelzi.
// Miért:      a kijelzőegység leírása szerint 3,5 V alatt USB kihúzásakor már újraindulás várható,
//             ezért a szintet kezelni kell. Becsült töltés közben nincs alacsony-akku hiba.
void battery_tick(PersistFilter& power_fault, uint32_t second) {
    uint16_t mv = 0;
    if (!g_adc_ok || !g_adc.read_mv(mv)) return;
    g_gauge.feed(mv, now_ms());
    const BatteryStatus b = g_gauge.status();
    {
        std::lock_guard<std::mutex> lock(g_mx);
        g_battery = b;
    }
    const bool low = b.level == BatteryLevel::Low || b.level == BatteryLevel::Critical;
    g_led->set_fault(Fault::Power, power_fault.update(low && !b.charging, now_ms()));
    if (low != g_dimmed) {
        g_dimmed = low;
        g_backlight_pct = low ? std::min<int>(g_cfg.brightness_pct, kLowBatteryBacklight) : g_cfg.brightness_pct;
        g_lcd->set_backlight(g_backlight_pct);
    }
    if (second % kBatteryLogPeriodS == 0)
        ESP_LOGI(TAG, "Akkumulátor: nyers %u mV, szűrt %u mV, %u %%, szint %d, töltés (becslés) %d", mv, b.mv,
                 b.percent, static_cast<int>(b.level), b.charging);
}

#if defined(CONFIG_OG_DISP_SLEEP)
// Mit csinál: light sleepbe teszi a kijelzőegységet; érintésre vagy az OBD-egység BLE-hirdetésére ébred.
// Hogyan:     az LVGL leáll, a háttérvilágítás és a panel kikapcsol, az ESP-NOW leáll. Az alvási szakaszok
//             (OG_WAKE_SCAN_PERIOD_MS) után rövid BLE-szkennelő ablak jön; az érintő megszakítás-lába
//             (alacsony szint) light sleepből is ébreszt. Ébredéskor minden visszaáll.
// Miért:      az autó leállása után a kijelző ne fogyassza az akkumulátort, de a vezető érintésére és
//             a gyújtás bekapcsolására (a C3 BLE-hirdetésével) azonnal ébredjen.
void enter_sleep() {
    const gpio_num_t ti = static_cast<gpio_num_t>(board::kTouch.intr);
    ESP_LOGI(TAG, "Alvás indul: %d s tétlenség után; ébredés érintésre vagy BLE-hirdetésre", CONFIG_OG_DISP_SLEEP_AFTER_S);
    g_led->set_enabled(false);
#if defined(CONFIG_OG_DISP_MUSIC)
    ble_stop();  // a telefonos kapcsolat megszűnik; az ébresztő ablakok saját BLE-indítást használnak
#endif
    lvgl_port_stop();
    g_lcd->set_backlight(0);
    g_lcd->display_on(false);
    g_link.suspend();
    ESP_LOGI(TAG, "Szabad belső RAM alvás előtt: %u kB (legnagyobb blokk %u kB)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << ti;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
    for (int i = 0; i < 100 && gpio_get_level(ti) == 0; ++i) vTaskDelay(pdMS_TO_TICKS(10));  // ujj még az érintőn
    gpio_wakeup_enable(ti, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    bool touched = false;
    uint32_t cycles = 0;
    const uint32_t t0 = now_ms();
    for (;;) {
        esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(CONFIG_OG_WAKE_SCAN_PERIOD_MS) * 1000);
        esp_light_sleep_start();
        ++cycles;
        if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
            touched = true;
            break;
        }
        const uint32_t before = ble_scan_stats().adverts;
        if (!ble_start_scanning(100)) {
            ESP_LOGW(TAG, "A BLE-szkennelés nem indult (szabad belső RAM %u kB, legnagyobb blokk %u kB)",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
            ble_stop();
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_OG_WAKE_SCAN_WINDOW_MS));
        const bool seen = ble_scan_stats().adverts != before;
        if (cycles == 1)
            ESP_LOGI(TAG, "Szabad belső RAM futó BLE mellett: %u kB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        ble_stop();
        if (cycles == 1)
            ESP_LOGI(TAG, "Szabad belső RAM a BLE leállítása után: %u kB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        if (seen) break;
    }
    gpio_wakeup_disable(ti);
    g_link.resume();
    g_lcd->display_on(true);
    g_lcd->set_backlight(g_backlight_pct);
    lvgl_port_resume();
    if (lvgl_port_lock(200)) {
        lv_obj_invalidate(lv_screen_active());
        lv_display_trigger_activity(nullptr);  // az ébresztő érintés ne számítson régi tétlenségnek
        lvgl_port_unlock();
    }
    g_led->set_enabled(true);
#if defined(CONFIG_OG_DISP_MUSIC)
    ble_ams_start("OpenGauge");  // az iPhone a kötött eszközhöz magától újracsatlakozik
#endif
    g_power.on_wake(now_ms());
    ESP_LOGI(TAG, "Ébredés: %s, %lu alvási szakasz, %lu ms után", touched ? "érintés" : "az OBD-egység hirdetése",
             (unsigned long)cycles, (unsigned long)(now_ms() - t0));
}
#endif

// Mit csinál: fogadja a csomagokat, frissíti a modellt, másodpercenként életjelet küld, méri az
//             akkumulátort, frissíti a LED-állapotot és naplóz.
// Hogyan:     a 0. magon fut; a zárat csak a modell frissítésének idejére veszi fel.
void link_task(void*) {
    PersistFilter radio_fault(kRadioFaultMs, kFaultClearMs);
    PersistFilter power_fault(kPowerFaultMs, kFaultClearMs);
    uint16_t hb_seq = 0;
    uint32_t second = 0;
    uint32_t next = now_ms() + 1000;
    for (;;) {
        const int32_t rem = static_cast<int32_t>(next - now_ms());
        EspNowLink::Packet p;
        if (rem > 0 && g_link.recv(p, static_cast<uint32_t>(rem))) {
            bool fresh, stopped, new_vin;
            uint32_t stopped_ms;
            char vin[18];
            {
                std::lock_guard<std::mutex> lock(g_mx);
                fresh = g_model.on_packet(p.data, p.len, p.rssi, p.rx_ms);
                stopped = g_model.engine_stopped();
                stopped_ms = g_model.stopped_ms();
                new_vin = g_model.take_new_vin(vin);
            }
            if (new_vin) {  // új vagy megváltozott VIN: mentjük a következő indítás logójához (a logót most nem cseréljük)
                const int brand = vin_brand_id(vin);
                ESP_LOGI(TAG, "VIN: %s (%s)", vin, brand >= 0 ? vin_brand_name(brand) : "ismeretlen márka");
                last_vin_save(vin);
            }
#if defined(CONFIG_OG_DISP_SLEEP)
            if (fresh) g_power.on_data(p.rx_ms);
            g_power.sync_engine(stopped, stopped_ms);
#else
            (void)fresh; (void)stopped; (void)stopped_ms;
#endif
            continue;
        }
        next += 1000;
        DashLink l;
        DashValues v;
        char cur_vin[18];
        {
            std::lock_guard<std::mutex> lock(g_mx);
            g_model.on_second();
            l = g_model.link();
            v = g_model.values(now_ms());
            std::memcpy(cur_vin, g_model.vin(), sizeof cur_vin);
        }
        ++second;
        const uint32_t now = now_ms();
        HeartbeatMsg hb = {};
        hb.h = make_header(MsgType::Heartbeat, hb_seq++);
        hb.uptime_s = now / 1000;
        g_link.send(&hb, sizeof hb);
        bool alive;
        {
            std::lock_guard<std::mutex> lock(g_mx);
            alive = g_model.peer_alive(now);
        }
        g_led->set_link(alive ? LinkLed::Ok : LinkLed::Waiting);
        g_led->set_fault(Fault::Radio, radio_fault.update(!alive, now));
        battery_tick(power_fault, second);
#if defined(CONFIG_OG_DISP_SLEEP)
        {
            // Az érintés ideje az LVGL tétlenségi számlálójából; az alvásig hátralévő idő a kijelzőre megy.
            uint32_t idle = 0;
            if (lvgl_port_lock(100)) {
                idle = lv_display_get_inactive_time(nullptr);
                lvgl_port_unlock();
            }
            g_power.on_touch(now > idle ? now - idle : 0);
            g_sleep_in_s.store(g_power.counting_down(now) ? g_power.seconds_left(now) : -1, std::memory_order_relaxed);
            if (g_power.should_sleep(now)) {
                enter_sleep();
                g_sleep_in_s.store(-1, std::memory_order_relaxed);
                next = now_ms();  // ébredés után azonnal életjel (a C3 ebből tudja, hogy leállíthatja a hirdetést)
                continue;
            }
        }
#endif

        const EspNowLink::Stats ls = g_link.stats();
        ESP_LOGI(TAG,
                 "%lu csomag/s, RSSI %d dBm | összes %lu, elveszett %lu (%.2f %%, 60 s: %.2f %%), elavult %lu, újraindulás %lu, "
                 "érvénytelen %lu, sor tele %lu, seq %u | adat %s, %lu 1/perc | FPS %lu, belső RAM %u kB | VIN %s",
                 (unsigned long)l.per_sec, l.rssi_dbm, (unsigned long)l.received, (unsigned long)l.lost,
                 l.loss_ratio * 100.0, l.loss_ratio_window * 100.0, (unsigned long)l.stale, (unsigned long)l.resyncs, (unsigned long)l.invalid,
                 (unsigned long)ls.rx_dropped, l.last_seq, v.fresh ? "friss" : "NINCS", (unsigned long)(v.rpm + 0.5f),
                 (unsigned long)lvgl_take_frames(), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), cur_vin[0] ? cur_vin : "--");
    }
}

#if defined(CONFIG_OG_DISP_BOOT_LOGO)
lv_obj_t* g_logo_scr = nullptr;

// Mit csinál: a logó időzítőjének lejártakor áttűnik a műszerfalra, és törli a logó képernyőt.
// Miért:      a logó csak indulásnál kell; a felszabadított képernyő nem foglal memóriát.
void logo_done(lv_timer_t* t) {
    lv_screen_load_anim(g_main.scr, LV_SCREEN_LOAD_ANIM_FADE_ON, kLogoFadeOutMs, 0, true);
    lv_timer_delete(t);
}

// Mit csinál: fekete képernyőt épít a középre tett fehér logóval, és betölti felúszással.
// Hogyan:     a kép A8 maszk, a színét az image_recolor adja; a műszerfalra váltást egyszeri időzítő indítja.
// Miért:      a 4 másodperc alatt a két egység párosodik és a BLE is elindul; az automatikus lapozás
//             addig nem kezd el váltani.
void show_boot_logo() {
    g_logo_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_logo_scr, g_bg, 0);
    lv_obj_set_style_bg_opa(g_logo_scr, LV_OPA_COVER, 0);
    lv_obj_t* img = lv_image_create(g_logo_scr);
    lv_image_set_src(img, &og_logo);
    lv_obj_set_style_image_recolor(img, g_text, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
    lv_screen_load_anim(g_logo_scr, LV_SCREEN_LOAD_ANIM_FADE_ON, kLogoFadeInMs, 0, false);
    lv_timer_create(logo_done, kLogoFadeInMs + kLogoHoldMs, nullptr);
    g_pause_until = now_ms() + kLogoFadeInMs + kLogoHoldMs + kLogoFadeOutMs + g_cfg.switch_period_s * 1000u;
}
#endif

}  // namespace

void run_dash_app(LcdPanel& lcd, bool swap_bytes, TouchPanel* touch, StatusLed& led) {
    g_led = &led;
    g_lcd = &lcd;
    if (!g_link.init(CONFIG_OG_ESPNOW_CHANNEL)) {
        ESP_LOGE(TAG, "Az ESP-NOW nem indult el");
        led.set_fault(Fault::Init, true);
        return;
    }
    led.set_link(LinkLed::Waiting);
    const bool cfg_loaded = settings_load(g_cfg);  // az ESP-NOW indítása már előkészítette az NVS-t
    apply_palette(g_cfg);
    g_backlight_pct = g_cfg.brightness_pct;
    ESP_LOGI(TAG, "Beállítások: %s, paletta: %s", cfg_loaded ? "mentett" : "alapértelmezett", palette_get(g_cfg.palette).name);
    g_adc_ok = g_adc.init(board::kPinBattery);
    lv_display_t* disp = lvgl_start(lcd, swap_bytes);
    if (!disp) return;
    if (touch && !lvgl_add_touch(disp, *touch)) ESP_LOGW(TAG, "Érintő nélkül fut: csak automatikus váltás");

    if (lvgl_port_lock(0)) {
        g_main.build();
        g_detail.build();
        g_linkv.build();
        g_screens[0] = g_main.scr;
        g_screens[1] = g_detail.scr;
        g_screens[2] = g_linkv.scr;
#if defined(CONFIG_OG_DISP_MUSIC)
        g_music.build();
        g_screens[kMusicId] = g_music.scr;
#endif
        for (lv_obj_t* scr : g_screens)
            if (scr) lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, nullptr);
        g_settings_view.build(UiColors{g_bg, g_track, g_text, g_dim, g_accent, g_warn}, on_settings_exit);

        g_cog = lv_button_create(lv_layer_top());
        lv_obj_set_size(g_cog, 56, 56);
        lv_obj_set_style_radius(g_cog, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_cog, g_track, 0);
        lv_obj_set_style_border_color(g_cog, g_accent, 0);
        lv_obj_set_style_border_width(g_cog, 2, 0);
        lv_obj_set_style_shadow_width(g_cog, 0, 0);
        lv_obj_align(g_cog, LV_ALIGN_BOTTOM_MID, 0, -14);
        make_label(g_cog, &og_font_montserrat_28, g_text, LV_ALIGN_CENTER, 0, 0, LV_SYMBOL_SETTINGS);
        lv_obj_add_flag(g_cog, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(g_cog, open_settings, LV_EVENT_CLICKED, nullptr);

        g_main.set_scale(g_cfg.max_rpm, g_cfg.redline_rpm);
        rebuild_active();
        set_perf_visible(g_cfg.debug_mode);
#if defined(CONFIG_OG_DISP_BOOT_LOGO)
        if (!g_cfg.skip_logo) show_boot_logo(); else lv_screen_load(g_main.scr);
#else
        lv_screen_load(g_main.scr);
#endif
        lv_timer_create(ui_tick, kUiPeriodMs, nullptr);
        g_switch_timer = lv_timer_create(switch_tick, g_cfg.switch_period_s * 1000u, nullptr);
        lvgl_port_unlock();
    }

#if defined(CONFIG_OG_DISP_MUSIC)
    // A BLE (iOS zene) mindig kész: a NimBLE host a PSRAM-ban fut (H-34), így belefér a belső RAM-ba.
    if (g_cfg.music_enabled && !ble_ams_start("OpenGauge")) ESP_LOGE(TAG, "A BLE nem indult el");
#endif

    // A kapcsolatfeladat a 0. magon (az LVGL az 1. magon fut).
    xTaskCreatePinnedToCore(link_task, "og_link", 4096, nullptr, 5, nullptr, 0);
    ESP_LOGI(TAG, "Műszerfal fut: 3 képernyő, váltás %lu ms-onként", (unsigned long)(g_cfg.switch_period_s * 1000u));
}

}  // namespace og
