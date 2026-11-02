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
#include "esp_timer.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_pattern.h"
#include "music_model.h"
#include "lvgl_setup.h"
#include "og_codec.h"
#include "sdkconfig.h"

namespace og {

namespace {

const char* TAG = "og_dash";

constexpr uint32_t kUiPeriodMs = 16;       // a nézetek frissítése (~60 Hz)
constexpr uint32_t kSwitchPeriodMs = 5000; // automatikus képernyőváltás
constexpr uint32_t kSwitchAnimMs = 400;
constexpr uint32_t kTouchPauseMs = 15000;  // érintés után ennyi ideig nincs automatikus váltás
constexpr int32_t kRpmMax = 8000;
constexpr int32_t kRpmRed = 6000;
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

lv_obj_t* make_screen() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
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

const lv_color_t kGrey = lv_color_hex(0x9A9A9A);
const lv_color_t kOrange = lv_color_hex(0xFF8000);
const lv_color_t kRed = lv_color_hex(0xE02020);

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

    void build() {
        scr = make_screen();
        arc = lv_arc_create(scr);
        lv_obj_set_size(arc, 320, 320);
        lv_obj_center(arc);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_range(arc, 0, kRpmMax);
        lv_arc_set_value(arc, 0);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_set_clickable(arc, false);
        lv_obj_set_style_arc_width(arc, 18, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 18, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(0x303030), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, kOrange, LV_PART_INDICATOR);

        rpm = make_label(scr, &lv_font_montserrat_48, lv_color_white(), LV_ALIGN_CENTER, 0, -30, "--");
        make_label(scr, &lv_font_montserrat_20, kGrey, LV_ALIGN_CENTER, 0, 10, "1/perc");
        speed = make_label(scr, &lv_font_montserrat_28, lv_color_white(), LV_ALIGN_CENTER, 0, 60, "-- km/h");
        nodata = make_label(scr, &lv_font_montserrat_20, kRed, LV_ALIGN_CENTER, 0, 110, "NINCS ADAT");
        battery = make_label(scr, &lv_font_montserrat_20, kGrey, LV_ALIGN_TOP_MID, 0, 62, "");
        sleepmsg = make_label(scr, &lv_font_montserrat_20, kOrange, LV_ALIGN_BOTTOM_MID, 0, -26, "");
    }

    void update(const DashValues& v, const BatteryStatus& bat, int sleep_in_s) {
        char b[24];
        if (sleep_in_s >= 0) std::snprintf(b, sizeof b, "ALVAS %d S", sleep_in_s);
        else b[0] = 0;
        set_text(sleepmsg, b);
        if (battery_shown(bat)) std::snprintf(b, sizeof b, "%s %u %%", battery_symbol(bat), bat.percent);
        else b[0] = 0;
        set_text(battery, b);
        const bool bat_red = battery_shown(bat) && bat.level != BatteryLevel::Ok;
        if (bat_red != battery_red) {
            lv_obj_set_style_text_color(battery, bat_red ? kRed : kGrey, 0);
            battery_red = bat_red;
        }
        const bool ok_rpm = v.fresh && (v.flags & kFlagRpm);
        const int32_t r = ok_rpm ? static_cast<int32_t>(v.rpm + 0.5f) : 0;
        if (lv_arc_get_value(arc) != r) lv_arc_set_value(arc, r);
        const bool want_red = r >= kRpmRed;
        if (want_red != red) {
            lv_obj_set_style_arc_color(arc, want_red ? kRed : kOrange, LV_PART_INDICATOR);
            red = want_red;
        }
        if (ok_rpm)
            std::snprintf(b, sizeof b, "%ld", static_cast<long>(r / 10 * 10));  // 10-es lépés: nyugodtabb szám
        else
            std::snprintf(b, sizeof b, "--");
        set_text(rpm, b);
        if (v.fresh && (v.flags & kFlagSpeed))
            std::snprintf(b, sizeof b, "%d km/h", static_cast<int>(v.speed_kmh + 0.5f));
        else
            std::snprintf(b, sizeof b, "-- km/h");
        set_text(speed, b);
        if (lv_obj_is_hidden(nodata) != v.fresh) lv_obj_set_hidden(nodata, v.fresh);
    }
};

// Egy "név – érték" sor a részletes és a kapcsolat nézethez.
struct Row {
    lv_obj_t* value;
    void build(lv_obj_t* scr, int32_t y, const char* name) {
        make_label(scr, &lv_font_montserrat_20, kGrey, LV_ALIGN_CENTER, -70, y, name);
        value = make_label(scr, &lv_font_montserrat_28, lv_color_white(), LV_ALIGN_CENTER, 65, y, "--");
    }
};

// (2) Részletek: hűtővíz, fojtószelep, a jármű feszültsége, MIL és hibakódok, a cella feszültsége.
struct DetailView {
    lv_obj_t* scr;
    Row coolant, throttle, vbat, mil, cell;
    bool mil_red = false;

    void build() {
        scr = make_screen();
        make_label(scr, &lv_font_montserrat_20, kOrange, LV_ALIGN_TOP_MID, 0, 45, "RESZLETEK");
        coolant.build(scr, -90, "VIZ");
        throttle.build(scr, -45, "FOJTO");
        vbat.build(scr, 0, "JARMU");
        mil.build(scr, 45, "MIL");
        cell.build(scr, 90, "CELLA");
    }

    void update(const DashValues& v, const BatteryStatus& bat) {
        char b[24];
        if (battery_shown(bat)) fmt_dec(b, sizeof b, "%.2f V", bat.mv / 1000.0);
        else std::snprintf(b, sizeof b, "--");
        set_text(cell.value, b);
        auto ok = [&](uint16_t f) { return v.fresh && (v.flags & f); };
        if (ok(kFlagCoolant)) std::snprintf(b, sizeof b, "%d °C", static_cast<int>(v.coolant_c));
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
            lv_obj_set_style_text_color(mil.value, want_red ? kRed : lv_color_white(), 0);
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
        make_label(scr, &lv_font_montserrat_20, kOrange, LV_ALIGN_TOP_MID, 0, 45, "KAPCSOLAT");
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
            lv_obj_set_style_text_color(age.value, v.fresh ? lv_color_white() : kRed, 0);
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
        lv_obj_set_style_bg_color(b, lv_color_hex(0x303030), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, on_music_button, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<uintptr_t>(cmd)));
        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
        lv_label_set_text(l, symbol);
        lv_obj_center(l);
        return l;
    }

    void build() {
        scr = make_screen();
        make_label(scr, &lv_font_montserrat_20, kOrange, LV_ALIGN_TOP_MID, 0, 45, "ZENE");
        title = make_label(scr, &lv_font_montserrat_28, lv_color_white(), LV_ALIGN_CENTER, 0, -60, "");
        artist = make_label(scr, &lv_font_montserrat_20, kGrey, LV_ALIGN_CENTER, 0, -22, "");
        for (lv_obj_t* l : {title, artist}) {
            lv_obj_set_width(l, 250);
            lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        }
        bar = lv_bar_create(scr);
        lv_obj_set_size(bar, 220, 8);
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, 22);
        lv_bar_set_range(bar, 0, 1000);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x303030), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, kOrange, LV_PART_INDICATOR);
        time = make_label(scr, &lv_font_montserrat_20, kGrey, LV_ALIGN_CENTER, 0, 48, "");
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
            std::snprintf(t, sizeof t, "PAROSITAS...");
            a[0] = 0;
        } else if (!i.title[0]) {
            std::snprintf(t, sizeof t, "NINCS ZENE");
            fold_ascii(i.player, a, sizeof a);
        } else {
            fold_ascii(i.title, t, sizeof t);
            fold_ascii(i.artist, a, sizeof a);
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
constexpr int kScreenCount = 4;  // a 4. (zene) képernyőre csak kézzel lépünk; az automatikus váltás az első háromban forog
#else
constexpr int kScreenCount = 3;
#endif
constexpr int kAutoScreens = 3;

MainView g_main;
DetailView g_detail;
LinkView g_linkv;
lv_obj_t* g_screens[kScreenCount];
int g_current = 0;
lv_timer_t* g_switch_timer = nullptr;
uint32_t g_pause_until = 0;

// Mit csinál: kiolvassa a modellt (rövid zár alatt), és frissíti mindhárom nézetet.
// Hogyan:     LVGL-időzítő, az LVGL-feladatban fut (az LVGL zárja már nála van).
// Miért:      a nem látható képernyők frissítése nem jár rajzolással, váltáskor viszont már
//             a friss értékek látszanak.
void ui_tick(lv_timer_t*) {
    DashValues v;
    DashLink l;
    BatteryStatus bat;
    {
        std::lock_guard<std::mutex> lock(g_mx);
        v = g_model.values(now_ms());
        l = g_model.link();
        bat = g_battery;
    }
    g_main.update(v, bat, g_sleep_in_s.load(std::memory_order_relaxed));
    g_detail.update(v, bat);
    g_linkv.update(v, l);
#if defined(CONFIG_OG_DISP_MUSIC)
    g_music.update(ble_ams_info(), now_ms());
#endif
}

// Mit csinál: a szomszédos képernyőre vált (step = +1: következő, -1: előző), csúszó animációval.
void show_next(int step) {
    g_current = (g_current + step + kScreenCount) % kScreenCount;
    lv_screen_load_anim(g_screens[g_current], step > 0 ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                        kSwitchAnimMs, 0, false);
}

// Mit csinál: automatikus váltás, ha az utolsó érintés óta eltelt kTouchPauseMs.
void switch_tick(lv_timer_t*) {
    if (static_cast<int32_t>(now_ms() - g_pause_until) < 0) return;
    if (g_current >= kAutoScreens) return;  // a zene képernyőről csak kézzel lépünk tovább
    if (g_current == kAutoScreens - 1 && kScreenCount > kAutoScreens) {
        // az utolsó automatikus képernyő után az elsőre ugrunk, a zene képernyőt kihagyva
        g_current = 0;
        lv_screen_load_anim(g_screens[0], LV_SCREEN_LOAD_ANIM_MOVE_LEFT, kSwitchAnimMs, 0, false);
        return;
    }
    show_next(+1);
}

// Mit csinál: balra húzás: következő képernyő, jobbra húzás: előző; utána szünetel az
//             automatikus váltás, hogy a kézzel választott képernyő ne tűnjön el azonnal.
void on_gesture(lv_event_t*) {
    lv_indev_t* in = lv_indev_active();
    if (!in) return;
    const lv_dir_t d = lv_indev_get_gesture_dir(in);
    if (d != LV_DIR_LEFT && d != LV_DIR_RIGHT) return;
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
    static bool dimmed = false;
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
    if (low != dimmed) {
        dimmed = low;
        g_backlight_pct = low ? std::min(CONFIG_OG_LCD_BACKLIGHT_PERCENT, kLowBatteryBacklight)
                              : CONFIG_OG_LCD_BACKLIGHT_PERCENT;
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
            bool fresh, stopped;
            uint32_t stopped_ms;
            {
                std::lock_guard<std::mutex> lock(g_mx);
                fresh = g_model.on_packet(p.data, p.len, p.rssi, p.rx_ms);
                stopped = g_model.engine_stopped();
                stopped_ms = g_model.stopped_ms();
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
        {
            std::lock_guard<std::mutex> lock(g_mx);
            g_model.on_second();
            l = g_model.link();
            v = g_model.values(now_ms());
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
                 "%lu csomag/s, RSSI %d dBm | összes %lu, elveszett %lu (%.2f %%), elavult %lu, újraindulás %lu, "
                 "érvénytelen %lu, sor tele %lu, seq %u | adat %s, %lu 1/perc | FPS %lu, belső RAM %u kB",
                 (unsigned long)l.per_sec, l.rssi_dbm, (unsigned long)l.received, (unsigned long)l.lost,
                 l.loss_ratio * 100.0, (unsigned long)l.stale, (unsigned long)l.resyncs, (unsigned long)l.invalid,
                 (unsigned long)ls.rx_dropped, l.last_seq, v.fresh ? "friss" : "NINCS", (unsigned long)(v.rpm + 0.5f),
                 (unsigned long)lvgl_take_frames(), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    }
}

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
        g_screens[3] = g_music.scr;
#endif
        for (lv_obj_t* scr : g_screens) lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, nullptr);
        lv_screen_load(g_main.scr);
        lv_timer_create(ui_tick, kUiPeriodMs, nullptr);
        g_switch_timer = lv_timer_create(switch_tick, kSwitchPeriodMs, nullptr);
        lvgl_port_unlock();
    }

#if defined(CONFIG_OG_DISP_MUSIC)
    // A BLE (iOS zene) mindig kész: a NimBLE host a PSRAM-ban fut (H-34), így belefér a belső RAM-ba.
    if (!ble_ams_start("OpenGauge")) ESP_LOGE(TAG, "A BLE nem indult el");
#endif

    // A kapcsolatfeladat a 0. magon (az LVGL az 1. magon fut).
    xTaskCreatePinnedToCore(link_task, "og_link", 4096, nullptr, 5, nullptr, 0);
    ESP_LOGI(TAG, "Műszerfal fut: 3 képernyő, váltás %lu ms-onként", (unsigned long)kSwitchPeriodMs);
}

}  // namespace og
