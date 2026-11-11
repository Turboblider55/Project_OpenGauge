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
#include <cmath>
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
#include "gear_model.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_pattern.h"
#include "music_model.h"
#include "og_fonts.h"
#include "og_logos.h"
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

#if defined(CONFIG_OG_DISP_MEMPROF)
// Mit csinál: egy sorban kiírja a memória állapotát a megadott lépés után, és az előző méréshez viszonyított változást.
// Hogyan:     a belső RAM és a PSRAM szabad és eddigi legkisebb szabad mérete, a legnagyobb összefüggő blokk, az LVGL
//             kupacának szabad/használt mérete és csúcshasználata. Az LVGL-adatokhoz az LVGL-zár (vagy az LVGL-feladat) kell.
// Miért:      a RAM-hiány okát csak mérés mutatja meg: lépésenként látszik, melyik képernyő mennyit visz el.
void mem_probe(const char* step) {
    static size_t prev_int = 0, prev_lv = 0, prev_ps = 0;
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const size_t fi = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t fp = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t lv_used = mon.total_size - mon.free_size;
    ESP_LOGI(TAG,
             "MEMPROF %-26s | belső: szabad %6u B (min %6u, blokk %6u, d %+d) | PSRAM: szabad %7u B (d %+d) | LVGL: használt %6u / %u B "
             "(csúcs %lu %%, töredezettség %u %%, d %+d)",
             step, (unsigned)fi, (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), prev_int ? (int)fi - (int)prev_int : 0, (unsigned)fp,
             prev_ps ? (int)fp - (int)prev_ps : 0, (unsigned)lv_used, (unsigned)mon.total_size,
             (unsigned long)(mon.max_used * 100ull / mon.total_size), (unsigned)mon.frag_pct, prev_lv ? (int)lv_used - (int)prev_lv : 0);
    prev_int = fi;
    prev_ps = fp;
    prev_lv = lv_used;
}

// Mit csinál: kilistázza a feladatokat és azt, mennyi vermük maradt soha nem használatlanul (a legkisebb szabad érték).
void mem_probe_tasks() {
#if defined(CONFIG_FREERTOS_USE_TRACE_FACILITY)
    TaskStatus_t st[24];
    const UBaseType_t n = uxTaskGetSystemState(st, 24, nullptr);
    for (UBaseType_t i = 0; i < n; ++i)
        ESP_LOGI(TAG, "MEMPROF feladat %-12s | verem szabad min. %5u B | prioritás %u", st[i].pcTaskName,
                 (unsigned)st[i].usStackHighWaterMark * (unsigned)sizeof(StackType_t), (unsigned)st[i].uxCurrentPriority);
#else
    ESP_LOGI(TAG, "MEMPROF feladat-lista: a CONFIG_FREERTOS_USE_TRACE_FACILITY ki van kapcsolva");
#endif
}

// Mit csinál: időnként újra mér (a váltogató képernyők használata közben), így az eddigi legkisebb szabad érték a csúcsot is mutatja.
void mem_probe_timer(lv_timer_t*) {
    mem_probe("periodikus");
    mem_probe_tasks();
}

// Mit csinál: ugyanaz, mint a mem_probe, de az LVGL-zárat is felveszi (az LVGL-en kívüli szálakból hívható).
void mem_probe_locked(const char* step) {
    if (lvgl_port_lock(1000)) {
        mem_probe(step);
        lvgl_port_unlock();
    }
}
#else
inline void mem_probe(const char*) {}
inline void mem_probe_locked(const char*) {}
#endif


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
// A fokozatbecslés (a kapcsolatfeladat tölti, a 0. magon; a kijelző az atomokból olvas). A modell zárja védi, mert a beállítás
// (fokozatszám) az LVGL-szálból módosítja.
GearModel g_gear;
std::mutex g_gear_mx;
std::atomic<int> g_gear_now{0};     // a becsült fokozat (0: nem ismert)
std::atomic<int> g_gear_known{0};   // a megtanult fokozatok száma
std::atomic<int> g_gear_total{5};   // a beállított fokozatszám
std::atomic<bool> g_gear_ready{false};
std::atomic<bool> g_gear_neutral{false};  // tartós gurulás üresben ("N")
uint32_t g_gear_saved_ms = 0;       // a tanult adat utolsó mentésének ideje
bool g_gear_unsaved = false;        // van mentetlen változás

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
lv_color_t g_bg, g_track, g_text, g_dim, g_accent, g_warn, g_needle;

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

// Mit csinál: egy friss telemetriamintát ad a fokozatbecslőnek, és kiteszi az eredményt az atomokba (a kijelző onnan olvas).
void gear_feed(const DashValues& v) {
    const bool ok = v.fresh && (v.flags & kFlagRpm) && (v.flags & kFlagSpeed);
    std::lock_guard<std::mutex> lock(g_gear_mx);
    g_gear.update(v.rpm, v.speed_kmh, ok, (v.flags & kFlagThrottle) ? v.throttle_pct : -1.f);
    g_gear_neutral.store(g_gear.neutral(), std::memory_order_relaxed);
    g_gear_now.store(g_gear.gear(), std::memory_order_relaxed);
    g_gear_known.store(g_gear.learned_gears(), std::memory_order_relaxed);
    g_gear_total.store(g_gear.config().gears, std::memory_order_relaxed);
    g_gear_ready.store(g_gear.ready(), std::memory_order_relaxed);
}

// Mit csinál: a beállított fokozatszámot átadja a becslőnek (a megtanult adat marad, ha a szám nem csökken).
void gear_apply_config() {
    std::lock_guard<std::mutex> lock(g_gear_mx);
    GearConfig c = g_gear.config();
    const bool changed = c.gears != g_cfg.gear_count;
    c.gears = g_cfg.gear_count;
    // Kézi módban a megadott áttételekből számol (ha mind megvan); automata módban a képlet kikapcsolt, a készülék tanul.
    c.final_drive = g_cfg.gear_manual ? static_cast<float>(g_cfg.final_drive_x100) / 100.f : 0.f;  // 0: nincs megadva
    c.tyre_circ_m = g_cfg.gear_manual ? static_cast<float>(g_cfg.tyre_circ_mm) / 1000.f : 0.f;
    for (int i = 0; i < kMaxGears; ++i) c.ratio[i] = g_cfg.gear_manual ? static_cast<float>(g_cfg.gear_ratio_x100[i]) / 100.f : 0.f;
    g_gear.set_config(c);
    if (changed) g_gear.reset_learning();  // más fokozatszám: más váltó, a tanult adat érvénytelen
    g_gear_total.store(g_gear.config().gears, std::memory_order_relaxed);
    g_gear_known.store(g_gear.learned_gears(), std::memory_order_relaxed);
    g_gear_ready.store(g_gear.ready(), std::memory_order_relaxed);
}

// Mit csinál: a tanult adatot elmenti, ha változott. A mentés NVS-be ír, ezért csak kGearSaveMinMs-onként (vagy kényszerítve).
// Miért:      a flash kopása: a tanulás percekig tart, és az adat lassan változik.
constexpr uint32_t kGearSaveMinMs = 120000;
void gear_persist(uint32_t now, bool force) {
    std::lock_guard<std::mutex> lock(g_gear_mx);
    if (g_gear.take_dirty()) g_gear_unsaved = true;
    if (!g_gear_unsaved || (!force && now - g_gear_saved_ms < kGearSaveMinMs)) return;
    if (gear_state_save(g_gear)) {
        g_gear_unsaved = false;
        g_gear_saved_ms = now;
        ESP_LOGI(TAG, "Tanult fokozatok mentve: %d/%d fokozat, %lu minta", g_gear.learned_gears(), g_gear.config().gears,
                 (unsigned long)g_gear.samples());
    }
}


// Mit csinál: a beállításban kiválasztott palettát LVGL-színekké alakítja.
void apply_palette(const Settings& s) {
    const Palette& p = palette_get(s.palette);
    g_bg = lv_color_hex(p.bg);
    g_track = lv_color_hex(p.track);
    g_text = lv_color_hex(p.text);
    g_dim = lv_color_hex(p.dim);
    g_accent = lv_color_hex(p.accent);
    g_warn = lv_color_hex(p.warn);
    g_needle = lv_color_hex(p.needle);
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

// (5) Mutatós műszerek közös alapja: 270°-os skála képbe rajzolva, forgatott élsimított tű, agy és "nincs adat" jelzés.
// Hogyan:     a skála osztásai és számai nem változnak, ezért egyszer képbe rajzoljuk (lv_snapshot, PSRAM),
//             és a képernyőn ez a kép látszik; így a tű mozgatásakor és a képernyő csúsztatásakor csak másolás kell,
//             nem a ~100 osztás és szám újrarajzolása. A skála egymásra tett lv_scale rétegekből áll (kis, közepes és
//             nagy osztások). A tű egy előre kiszámolt (élsimított, szimmetrikus) A8 kép, amelyet az LVGL forgat;
//             mozgásakor csak az érintett terület rajzolódik újra. A tű a mért érték felé simítva mozog.
struct NeedleGaugeBase {
    static constexpr int32_t kDiameter = 336;
    static constexpr int32_t kNeedleLen = 126;  // a tű hossza a középponttól a csúcsig
    static constexpr int32_t kNeedleHalfW = 6;  // a tű fél szélessége a tövénél (a csúcs felé egyenletesen szűkül)
    static constexpr int32_t kTipHalfW = 2;     // a tű fél szélessége a csúcsnál (tompa vég)
    static constexpr int32_t kTailLen = 30;     // a tű farka az agyon túl
    static constexpr int32_t kTailW = 9;        // a farok vastagsága
    static constexpr int32_t kImgW = 16;        // a tű képének szélessége
    static constexpr int32_t kImgPivotY = kNeedleLen + 2;  // a forgáspont magassága a képben (a csúcs fölött 2 képpont ráhagyás)
    static constexpr int32_t kImgH = kImgPivotY + kTailLen + 2;
    static constexpr int32_t kCenter = 180;     // a képernyő közepe (360 x 360)
    static constexpr float kSmooth = 0.35f;     // a tű képkockánként ennyivel közelít a célhoz

    lv_obj_t* scr;
    lv_obj_t* face = nullptr;                   // a kész skála képe
    lv_obj_t* live_scale = nullptr;             // csak ha a képpuffer nem foglalható le: az élő skála marad
    lv_obj_t* needle;
    lv_obj_t* value;                            // a számjegyes érték a tű alatt
    lv_obj_t* nodata;
    lv_draw_buf_t face_buf;
    void* face_mem = nullptr;
    lv_draw_buf_t* needle_buf = nullptr;        // a tű képe (A8)
    lv_style_t red_main, red_major, red_med, red_minor;
    float shown = 0;
    int32_t drawn = -1;
    int32_t gmax = 8000;                        // a skála vége (a mérő mértékegységében)
    int32_t start_deg = 135;                    // a skála kezdő iránya (fok, az óramutató járásával, a 3 óra felől mérve)
    int32_t sweep_deg = 270;                    // a skála nyílása; negatív: az érték az óramutatóval szemben nő

    // Mit csinál: egy skálaréteget hoz létre a tartón (átlátszó háttér, 270°-os körív 135°-tól).
    // Hogyan:     a skála végét az osztásköz többszörösére kerekíti lefelé, a szögtartományt arányosan csökkenti,
    //             így az osztások pontosan az értékekre esnek.
    lv_obj_t* add_scale(lv_obj_t* host, int32_t step) {
        const int32_t top = gmax / step * step;
        lv_obj_t* sc = lv_scale_create(host);
        lv_obj_set_size(sc, kDiameter, kDiameter);
        lv_obj_center(sc);
        lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sc, 0, 0);
        lv_scale_set_mode(sc, LV_SCALE_MODE_ROUND_INNER);
        lv_scale_set_angle_range(sc, static_cast<uint32_t>(std::abs(sweep_deg) * top / gmax));
        lv_scale_set_rotation(sc, static_cast<int32_t>(sweep_deg >= 0 ? start_deg : start_deg + sweep_deg));
        lv_scale_set_total_tick_count(sc, static_cast<uint32_t>(top / step + 1));
        lv_scale_set_range(sc, 0, top);
        lv_obj_set_style_arc_width(sc, 0, LV_PART_MAIN);
        return sc;
    }

    // Mit csinál: egy számot tesz a skála osztása mellé, belül, a felirat méretéhez és helyzetéhez igazítva.
    // Hogyan:     a felirat doboza és az osztás vége közti rés mindenhol ugyanakkora (gap): a doboz sugárirányú kiterjedése
    //             a fél szélesség és a fél magasság vetülete (|cos| és |sin| szerint). Így a hosszú szám a kör két oldalán
    //             (ahol a szélessége sugárirányú) beljebb kerül, a kör tetején és alján (ahol csak a magassága számít) nem.
    // Miért:      az LVGL saját felirata a középpontot rögzíti, ezért a vízszintesen elhelyezkedő hosszú számok rálógtak az osztásra.
    void add_label(lv_obj_t* host, int32_t val, const char* text, const lv_font_t* font, lv_color_t color, int32_t tick_len, int32_t gap) {
        lv_point_t sz;
        lv_text_get_size(&sz, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const float th = (static_cast<float>(start_deg) + static_cast<float>(sweep_deg) * static_cast<float>(val) / static_cast<float>(gmax)) * 3.14159265f / 180.f;
        const float c = std::cos(th), sn = std::sin(th);
        const float ext = 0.5f * static_cast<float>(sz.x) * std::fabs(c) + 0.35f * static_cast<float>(sz.y) * std::fabs(sn);  // a számjegyek a sor magasságának ~60%-át töltik ki
        const float r = kDiameter / 2.f - static_cast<float>(tick_len + gap) - ext;
        make_label(host, font, color, LV_ALIGN_CENTER, static_cast<int32_t>(std::lround(r * c)), static_cast<int32_t>(std::lround(r * sn)), text);
    }

    // Mit csinál: körívet rajzol a skála mentén a két érték között (a skála külső szélén, befelé vastagodva).
    // Hogyan:     lv_arc objektum, csak a háttérív látszik (a jelző és a gomb ki van kapcsolva).
    // Miért:      a skálaobjektum saját ívét a pillanatkép nem mutatta, ez az ív minden esetben látszik.
    void add_arc(lv_obj_t* host, int32_t from_val, int32_t to_val, int32_t width, lv_color_t color) {
        lv_obj_t* a = lv_arc_create(host);
        lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
        lv_obj_set_size(a, kDiameter, kDiameter);
        lv_obj_center(a);
        lv_obj_remove_flag(a, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE));
        int32_t s0 = start_deg + sweep_deg * from_val / gmax, s1 = start_deg + sweep_deg * to_val / gmax;
        if (s0 > s1) std::swap(s0, s1);  // az LVGL íve mindig az óramutatóval megy s0-tól s1-ig
        lv_arc_set_bg_angles(a, static_cast<lv_value_precise_t>((s0 % 360 + 360) % 360), static_cast<lv_value_precise_t>((s1 % 360 + 360) % 360));
        lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, color, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, 0, LV_PART_INDICATOR);
    }

    // Mit csinál: a skála rajzolásához tartót készít a képernyőn (a régi élő skála, ha volt, törlődik).
    // Hogyan:     a skála a rajzterületét 100 px-szel túlnyújtja; a pontosan kDiameter méretű tartóról készül a kép.
    lv_obj_t* begin_face() {
        if (live_scale) {
            lv_obj_delete(live_scale);
            live_scale = nullptr;
        }
        lv_obj_t* host = lv_obj_create(scr);
        lv_obj_remove_style_all(host);
        lv_obj_set_size(host, kDiameter, kDiameter);
        lv_obj_center(host);
        lv_obj_set_style_bg_color(host, g_bg, 0);
        lv_obj_set_style_bg_opa(host, LV_OPA_COVER, 0);
        return host;
    }

    // Mit csinál: a tartót képbe rajzolja (snapshot) és a képernyőn ezt a képet mutatja; a tartó törlődik.
    void end_face(lv_obj_t* host) {
        lv_obj_update_layout(host);
        if (face_mem && lv_snapshot_take_to_draw_buf(host, LV_COLOR_FORMAT_RGB565, &face_buf) == LV_RESULT_OK) {
            lv_image_set_src(face, &face_buf);
            lv_obj_invalidate(face);
            lv_obj_delete(host);
        } else {
            ESP_LOGW(TAG, "A skála képe nem készült el, az élő skála marad");
            live_scale = host;
            lv_obj_move_to_index(host, 0);
        }
        drawn = -1;  // a tűt az új skálán újra ki kell számolni
    }

    // Mit csinál: igaz, ha a pont a lekerekített téglalapban van (x0,y0)-(x1,y1), r sugarú sarkokkal.
    static bool in_rrect(float x, float y, float x0, float y0, float x1, float y1, float r) {
        if (x < x0 || x > x1 || y < y0 || y > y1) return false;
        const float cx = x < x0 + r ? x0 + r : (x > x1 - r ? x1 - r : x);
        const float cy = y < y0 + r ? y0 + r : (y > y1 - r ? y1 - r : y);
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
    }

    static bool in_circle(float x, float y, float cx, float cy, float r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }

    // Mit csinál: egy w x h-s A8 ikont (átlátszósági maszk) rajzol egy alakzatfüggvényből: inside(x, y) igaz, ahol az ikon festett.
    // Hogyan:     minden képpontot 4x4 mintavétellel számol (mint a tű), így a széle élsimított; a színt az image_recolor adja.
    // Miért:      az ikonok kevés memóriát visznek (w*h bájt), a palettával színezhetők, és nincs szükség képfájlra.
    template <typename F>
    static lv_draw_buf_t* make_icon(int32_t w, int32_t h, F inside) {
        lv_draw_buf_t* b = lv_draw_buf_create(static_cast<uint32_t>(w), static_cast<uint32_t>(h), LV_COLOR_FORMAT_A8, 0);
        if (!b) return nullptr;
        for (int32_t y = 0; y < h; ++y) {
            uint8_t* row = static_cast<uint8_t*>(b->data) + static_cast<size_t>(y) * b->header.stride;
            for (int32_t x = 0; x < w; ++x) {
                int hit = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) hit += inside(static_cast<float>(x) + (sx + 0.5f) / 4.f, static_cast<float>(y) + (sy + 0.5f) / 4.f) ? 1 : 0;
                row[x] = static_cast<uint8_t>(hit * 255 / 16);
            }
        }
        return b;
    }

    // Mit csinál: egy ikont tesz a képernyőre a középponthoz képest (x, y) helyre, a palettaszínnel.
    lv_obj_t* add_icon(lv_draw_buf_t* buf, int32_t x, int32_t y) {
        lv_obj_t* img = lv_image_create(scr);
        if (buf) lv_image_set_src(img, buf);
        lv_obj_set_style_image_recolor(img, g_accent, 0);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
        lv_obj_remove_flag(img, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_align(img, LV_ALIGN_CENTER, x, y);
        return img;
    }

    // Mit csinál: a tű képét (A8 átlátszósági maszk, függőleges, a csúcs felül) előállítja egyszer.
    // Hogyan:     minden képpontot 4x4 mintavétellel (supersampling) számol az alakzatból: a törzs a tövétől (kNeedleHalfW)
    //             a csúcsig (kTipHalfW) egyenletesen keskenyedik, a farok állandó vastagságú és lekerekített végű.
    //             A kép a tű tengelyére szimmetrikus, mert az alakzatot a tengelyhez képest számoljuk.
    // Miért:      a kész képet az LVGL egyetlen forgatással, törtképpont-pontosan (élsimítva) rajzolja; így nincs külön
    //             szár, hegy és farok, amelyek különbözően kerekednének egész képpontra.
    void make_needle_image() {
        needle_buf = lv_draw_buf_create(kImgW, kImgH, LV_COLOR_FORMAT_A8, 0);
        if (!needle_buf) {
            ESP_LOGE(TAG, "A tű képének memóriája nem foglalható le");
            return;
        }
        const float cx = kImgW / 2.0f, py = static_cast<float>(kImgPivotY);
        const float tail_r = kTailW / 2.0f, tail_end = kTailLen - tail_r;  // a farok félkör-végének középpontja
        for (int y = 0; y < kImgH; ++y) {
            uint8_t* row = static_cast<uint8_t*>(needle_buf->data) + static_cast<size_t>(y) * needle_buf->header.stride;
            for (int x = 0; x < kImgW; ++x) {
                int hit = 0;
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        const float u = std::abs(x + (sx + 0.5f) / 4 - cx);   // a tengelytől mért távolság
                        const float v = py - (y + (sy + 0.5f) / 4);          // a középponttól a csúcs felé mért hosszanti hely
                        bool in;
                        if (v >= 0) {
                            in = v <= kNeedleLen && u <= kNeedleHalfW + (kTipHalfW - kNeedleHalfW) * v / kNeedleLen;
                        } else if (-v <= tail_end) {
                            in = u <= tail_r;
                        } else {
                            const float dv = -v - tail_end;
                            in = u * u + dv * dv <= tail_r * tail_r;
                        }
                        hit += in;
                    }
                }
                row[x] = static_cast<uint8_t>(hit * 255 / 16);
            }
        }
    }

    // Mit csinál: a tű forgatása tized fokban a megadott értékhez (a skála 135°-nál kezdődik, 270° nyílású).
    // Hogyan:     a kép csúcsa felfelé néz (-90°), ezért a forgatás az irányszög + 90°.
    int32_t needle_rotation(float val) const { return rotation_of(val, gmax, start_deg, sweep_deg); }

    // Mit csinál: a tű forgatása tized fokban egy tetszőleges nyílású skálán (az érték 0..max).
    static int32_t rotation_of(float val, int32_t max, int32_t start, int32_t sweep) {
        const int32_t r = start * 10 + 900 + static_cast<int32_t>(std::lround(static_cast<float>(sweep) * 10.f * val / static_cast<float>(max)));
        return (r % 3600 + 3600) % 3600;
    }

    // Mit csinál: a közös elemeket (képernyő, színes stílusok, skálakép, tű, agy, feliratok) felépíti.
    // Hogyan:     a mérőnként eltérő feliratokat az utódosztály adja a skála képébe.
    void build_base() {
        build_face_part();
        build_needle_part();
    }

    // Mit csinál: a képernyőt, a piros szakaszok stílusait és a skálakép képét (üresen) építi fel.
    void build_face_part() {
        scr = make_screen();
        lv_obj_set_style_pad_all(scr, 0, 0);
        lv_obj_set_style_border_width(scr, 0, 0);

        lv_style_init(&red_main);
        lv_style_set_arc_color(&red_main, g_warn);
        lv_style_set_arc_width(&red_main, 8);
        lv_style_init(&red_major);
        lv_style_set_line_color(&red_major, g_warn);
        lv_style_set_line_width(&red_major, 3);
        lv_style_set_text_color(&red_major, g_warn);
        lv_style_init(&red_med);
        lv_style_set_line_color(&red_med, g_warn);
        lv_style_set_line_width(&red_med, 2);
        lv_style_init(&red_minor);
        lv_style_set_line_color(&red_minor, g_warn);
        lv_style_set_line_width(&red_minor, 2);

        const uint32_t bytes = kDiameter * kDiameter * 2;
        face_mem = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM);
        if (face_mem) lv_draw_buf_init(&face_buf, kDiameter, kDiameter, LV_COLOR_FORMAT_RGB565, kDiameter * 2, face_mem, bytes);
        else ESP_LOGE(TAG, "A skála képének memóriája nem foglalható le");
        face = lv_image_create(scr);
        lv_obj_center(face);
    }

    // Mit csinál: a tűt, az agyat és a feliratokat (érték, NINCS ADAT) építi fel a skálakép fölé.
    void build_needle_part() {
        // A tű egy előre kiszámolt, forgatott A8 kép; a középpontja a képernyő közepén van, a színét a paletta adja.
        make_needle_image();
        needle = lv_image_create(scr);
        if (needle_buf) lv_image_set_src(needle, needle_buf);
        lv_obj_set_pos(needle, kCenter - kImgW / 2, kCenter - kImgPivotY);
        lv_image_set_pivot(needle, kImgW / 2, kImgPivotY);
        lv_obj_set_style_image_recolor(needle, g_needle, 0);
        lv_obj_set_style_image_recolor_opa(needle, LV_OPA_COVER, 0);
        lv_obj_remove_flag(needle, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_image_set_rotation(needle, needle_rotation(0));
        lv_obj_t* hub = lv_obj_create(scr);
        lv_obj_remove_style_all(hub);
        lv_obj_set_size(hub, 30, 30);
        lv_obj_center(hub);
        lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(hub, g_track, 0);
        lv_obj_set_style_border_width(hub, 3, 0);
        lv_obj_set_style_border_color(hub, g_accent, 0);

        // Fix szélességű, középre igazított feliratok: a tartalomhoz igazodó szélesség eltolta az értéket.
        value = make_label(scr, &og_font_montserrat_28, g_text, LV_ALIGN_CENTER, 0, 85, "--");
        nodata = make_label(scr, &og_font_montserrat_20, g_warn, LV_ALIGN_CENTER, 0, 128, "NINCS ADAT");
        for (lv_obj_t* l : {value, nodata}) {
            lv_obj_set_width(l, 200);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        }
        lv_obj_align(value, LV_ALIGN_CENTER, 0, 85);
        lv_obj_align(nodata, LV_ALIGN_CENTER, 0, 128);
    }

    // Mit csinál: a tűt a cél felé simítva mozgatja, és csak akkor rajzoltatja újra, ha a forgatás (0,1°) változott.
    void move_needle(float target) {
        shown += (target - shown) * kSmooth;
        if (std::abs(target - shown) < static_cast<float>(gmax) / 2700.f * 0.5f) shown = target;
        const int32_t rot = needle_rotation(shown);
        if (rot != drawn) {
            lv_image_set_rotation(needle, rot);
            drawn = rot;
        }
    }

    // Mit csinál: a "NINCS ADAT" felirat láthatóságát az adatok frissességéhez igazítja.
    void show_nodata(bool fresh) {
        if (lv_obj_is_hidden(nodata) != fresh) lv_obj_set_hidden(nodata, fresh);
    }
};

// Mit csinál: mutatós fordulatszámmérő: ezres számok, 500-as közepes osztások halvány felirattal, a redline-tól piros sáv;
//             a tű alatt nincs külön szám (a sebességnek külön mutatós képernyője van).
// Hogyan:     három skálaréteg: sűrű kis osztások (100, 7000 fölött 200 1/perc), 500-as osztások és az ezres nagy osztások.
struct NeedleTachView : NeedleGaugeBase {
    int32_t rpm_red = 6500;

    // Mit csinál: a skála kész képét előállítja a jelenlegi skálavéggel és redline-nal.
    void render_face() {
        lv_obj_t* host = begin_face();

        // Körívek: vékony, 270°-os íve (nem teljes kör) és a redline-tól a piros sáv.
        add_arc(host, 0, gmax, 2, g_accent);
        if (rpm_red < gmax) add_arc(host, rpm_red, gmax, 8, g_warn);

        // 1. réteg: sűrű kis osztások; 7000 1/perc fölött 200-as osztásköz.
        const bool dense = gmax <= 7000;
        const int32_t fine_step = dense ? 100 : 200;
        lv_obj_t* fine = add_scale(host, fine_step);
        const int32_t fine_top = gmax / fine_step * fine_step;
        lv_scale_set_major_tick_every(fine, static_cast<uint32_t>(fine_top / fine_step + 1));  // csak a 0. osztás "nagy"
        lv_obj_set_style_line_width(fine, 2, LV_PART_ITEMS);
        lv_obj_set_style_line_color(fine, g_dim, LV_PART_ITEMS);
        lv_obj_set_style_length(fine, 8, LV_PART_ITEMS);
        lv_obj_set_style_line_width(fine, 2, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(fine, g_dim, LV_PART_INDICATOR);
        lv_obj_set_style_length(fine, 8, LV_PART_INDICATOR);
        lv_scale_section_t* z_fine = lv_scale_add_section(fine);
        lv_scale_set_section_range(fine, z_fine, std::min(rpm_red, fine_top), fine_top);
        lv_scale_set_section_style_indicator(fine, z_fine, &red_minor);
        lv_scale_set_section_style_items(fine, z_fine, &red_minor);

        // 2. réteg: az 500-as osztások, kicsit hosszabbak a kicsiknél (csak a 100-as osztásköznél, mert a 200-as nem illeszkedik).
        if (dense) {
            lv_obj_t* med = add_scale(host, 500);
            lv_scale_set_major_tick_every(med, 1);
            lv_obj_set_style_line_width(med, 2, LV_PART_INDICATOR);
            lv_obj_set_style_line_color(med, g_accent, LV_PART_INDICATOR);
            lv_obj_set_style_length(med, 12, LV_PART_INDICATOR);
            lv_scale_set_label_show(med, false);  // a feliratokat az add_label teszi ki
            lv_scale_section_t* z_med = lv_scale_add_section(med);
            lv_scale_set_section_range(med, z_med, std::min(rpm_red, gmax / 500 * 500), gmax / 500 * 500);
            lv_scale_set_section_style_indicator(med, z_med, &red_med);
        }

        // 3. réteg: az ezres nagy osztások a számokkal.
        lv_obj_t* big = add_scale(host, 1000);
        lv_scale_set_major_tick_every(big, 1);
        lv_scale_set_label_show(big, false);
        lv_obj_set_style_line_width(big, 3, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(big, g_text, LV_PART_INDICATOR);
        lv_obj_set_style_length(big, 16, LV_PART_INDICATOR);
        lv_scale_section_t* z_big = lv_scale_add_section(big);
        lv_scale_set_section_range(big, z_big, std::min(rpm_red, gmax / 1000 * 1000), gmax / 1000 * 1000);
        lv_scale_set_section_style_indicator(big, z_big, &red_major);

        // Feliratok: az ezres számok nagyok (a redline-tól pirosak), a félezresek kicsik és halványak.
        char b[12];
        for (int32_t v = 0; v <= gmax / 1000 * 1000; v += 1000) {
            std::snprintf(b, sizeof b, "%d", static_cast<int>(v / 1000));
            add_label(host, v, b, &og_font_montserrat_20, v >= rpm_red ? g_warn : g_text, 16, 5);
        }
        if (dense) {
            for (int32_t v = 500; v <= gmax / 500 * 500; v += 1000) {
                std::snprintf(b, sizeof b, "%d", static_cast<int>(v));
                add_label(host, v, b, &lv_font_montserrat_14, v >= rpm_red ? g_warn : g_dim, 12, 5);
            }
        }
        make_label(host, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, 0, 50, "X1000 1/perc");
        end_face(host);
    }

    // Mit csinál: új skálavég és redline (beállítás módosításakor): újragenerálja a skálát.
    void set_scale(int32_t max, int32_t redline) {
        gmax = std::max<int32_t>(1000, max / 100 * 100);
        rpm_red = std::min(redline, gmax);
        render_face();
    }

    lv_obj_t* gear_lbl = nullptr;

    void build() {
        build_base();
        gear_lbl = make_label(scr, &og_font_montserrat_20, g_accent, LV_ALIGN_CENTER, 0, 118, "");
        lv_obj_set_width(gear_lbl, 200);
        lv_obj_set_style_text_align(gear_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(gear_lbl, LV_ALIGN_CENTER, 0, 118);
    }

    void update(const DashValues& v) {
        const bool ok_rpm = v.fresh && (v.flags & kFlagRpm);
        move_needle(ok_rpm ? std::min<float>(v.rpm, static_cast<float>(gmax)) : 0.f);
        {   // fokozat: ismert fokozatnál "3. fokozat", különben ("-" váltás közben, "Tanulás n/N" amíg nem ismert minden fokozat)
            char g[24];
            if (!v.fresh) g[0] = 0;
            else if (g_gear_neutral.load(std::memory_order_relaxed)) std::snprintf(g, sizeof g, "N (üres)");
            else if (!g_gear_ready.load(std::memory_order_relaxed)) std::snprintf(g, sizeof g, "Tanulás %d/%d", g_gear_known.load(std::memory_order_relaxed), g_gear_total.load(std::memory_order_relaxed));
            else if (g_gear_now.load(std::memory_order_relaxed) > 0) std::snprintf(g, sizeof g, "%d. fokozat", g_gear_now.load(std::memory_order_relaxed));
            else std::snprintf(g, sizeof g, "-");
            set_text(gear_lbl, g);
        }
        char b[12];
        if (ok_rpm) std::snprintf(b, sizeof b, "%d", static_cast<int>((v.rpm + 5.f) / 10.f) * 10);  // 10-es kerekítés: a számjegyek nem villognak
        else std::snprintf(b, sizeof b, "--");
        set_text(value, b);
        show_nodata(v.fresh);
    }
};

// Mit csinál: mutatós sebességmérő: számozott nagy osztások, közepes és kis osztások; a tű alatt a számjegyes érték.
// Hogyan:     a skála végét a beállított végsebesség adja a kiválasztott mértékegységben (km/h vagy mph). Az osztások
//             sűrűsége a skála végétől függ, hogy a számok ne torlódjanak: 140-ig (1, 5, 10), 280-ig (5, 10, 20),
//             fölötte (10, 20, 40). Beállítás vagy mértékegység módosításakor újragenerálódik (set_scale).
struct NeedleSpeedView : NeedleGaugeBase {
    bool mph = false;
    bool rendered = false;
    int32_t max_kmh = 0;                  // a legutóbb beállított végsebesség (km/h), a változás észleléséhez

    // Mit csinál: a skála kész képét előállítja a jelenlegi skálavéggel és mértékegységgel.
    void render_face(int32_t fine_step, int32_t med_step, int32_t big_step) {
        lv_obj_t* host = begin_face();

        add_arc(host, 0, gmax, 2, g_accent);  // a vékony körív

        // 1. réteg: kis osztások.
        lv_obj_t* fine = add_scale(host, fine_step);
        lv_scale_set_major_tick_every(fine, static_cast<uint32_t>(gmax / fine_step + 1));  // csak a 0. osztás "nagy"
        lv_obj_set_style_line_width(fine, 2, LV_PART_ITEMS);
        lv_obj_set_style_line_color(fine, g_dim, LV_PART_ITEMS);
        lv_obj_set_style_length(fine, 8, LV_PART_ITEMS);
        lv_obj_set_style_line_width(fine, 2, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(fine, g_dim, LV_PART_INDICATOR);
        lv_obj_set_style_length(fine, 8, LV_PART_INDICATOR);

        // 2. réteg: közepes osztások.
        lv_obj_t* med = add_scale(host, med_step);
        lv_scale_set_major_tick_every(med, 1);
        lv_obj_set_style_line_width(med, 2, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(med, g_accent, LV_PART_INDICATOR);
        lv_obj_set_style_length(med, 12, LV_PART_INDICATOR);
        lv_scale_set_label_show(med, false);

        // 3. réteg: nagy osztások, számokkal.
        lv_obj_t* big = add_scale(host, big_step);
        lv_scale_set_major_tick_every(big, 1);
        lv_scale_set_label_show(big, false);  // a feliratokat az add_label teszi ki
        lv_obj_set_style_line_width(big, 3, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(big, g_text, LV_PART_INDICATOR);
        lv_obj_set_style_length(big, 16, LV_PART_INDICATOR);
        char b[12];
        for (int32_t v = 0; v <= gmax; v += big_step) {
            std::snprintf(b, sizeof b, "%d", static_cast<int>(v));
            add_label(host, v, b, &og_font_montserrat_20, g_text, 16, 5);
        }

        make_label(host, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, 0, 50, mph ? "mph" : "km/h");
        end_face(host);
        rendered = true;
    }

    // Mit csinál: beállítja a skálavéget és a mértékegységet; csak változáskor generálja újra a skálát.
    // Hogyan:     a végsebességet átszámolja a mértékegységre, és lefelé kerekíti a nagy osztás többszörösére.
    void set_scale(int32_t max_speed_kmh, bool use_mph) {
        if (rendered && use_mph == mph && max_speed_kmh == max_kmh) return;
        mph = use_mph;
        max_kmh = max_speed_kmh;
        const int32_t v = mph ? static_cast<int32_t>(max_speed_kmh * 0.621371f + 0.5f) : max_speed_kmh;
        int32_t fine = 10, med = 20, big = 40;
        if (v <= 140) { fine = 1; med = 5; big = 10; }
        else if (v <= 280) { fine = 5; med = 10; big = 20; }
        gmax = std::max(big, v / big * big);
        render_face(fine, med, big);
    }

    void build() {
        gmax = 240;
        build_base();
    }

    void update(const DashValues& v) {
        const bool ok = v.fresh && (v.flags & kFlagSpeed);
        const float val = ok ? (mph ? v.speed_kmh * 0.621371f : v.speed_kmh) : 0.f;
        move_needle(std::min(val, static_cast<float>(gmax)));
        char b[16];
        if (ok) std::snprintf(b, sizeof b, "%d", static_cast<int>(val + 0.5f));
        else std::snprintf(b, sizeof b, "--");
        set_text(value, b);
        show_nodata(v.fresh);
    }
};

// Mit csinál: kettős mutatós mérő egy tárcsán: felül az akkumulátorfeszültség (10–15 V), alul a hűtővíz hőmérséklete;
//             a két tű közös tengelyen mozog, a számértékek a tárcsa két oldalán látszanak.
// Hogyan:     a skála két 90°-os ív (felül és alul is balról jobbra nő az érték), a közös alap skála-, címke- és
//             körív-segédjei a start_deg/sweep_deg tagokon át használhatók tetszőleges nyílásra. Az értékek pozíciója a
//             skála elejétől számolt egész szám (feszültségnél tized volt, hőmérsékletnél fok), a szöveg ebből áll elő.
//             Az akkumulátor-skála a mért feszültségtől függ (12 V-os vagy 24 V-os rendszer, 18 V fölött 24 V-os; a váltáskor
//             a tárcsa újrarajzolódik, közte hiszterézis van). 12 V: 10..16 V, piros <= 11,0 V és >= 15,0 V; 24 V: 23..30 V,
//             piros <= 23,5 V és >= 29,5 V. Hűtővíz: 60..120 °C 15 fokos nagy osztásokkal, piros 105 fölött.
struct NeedleDualView : NeedleGaugeBase {
    static constexpr int32_t kBatStart = 225, kBatSweep = 90;                 // a feszültségmérő íve felül
    static constexpr float kV24On = 20.0f, kV24Off = 17.0f;                   // a 24 V-os rendszer felismerésének hiszterézise (V)
    static constexpr int32_t kTempStart = 135, kTempSweep = -90;              // alul balról jobbra
    lv_obj_t* needle2 = nullptr;
    lv_obj_t* value2 = nullptr;
    lv_draw_buf_t* icon_bat = nullptr;   // az akkumulátor és a hőmérő ikonja (A8)
    lv_draw_buf_t* icon_temp = nullptr;
    float shown2 = 0;
    int32_t drawn2 = -1;
    bool fahrenheit = false;
    bool rendered = false;
    int32_t t_min = 60, t_span = 60;  // a hőmérséklet-skála kezdete és nyílása a kijelzett mértékegységben
    bool v24 = false;                 // 24 V-os rendszer (a mért feszültségből vagy a beállításból)
    SystemVoltage mode = SystemVoltage::Auto;
    int32_t b_min = 100, b_span = 60; // a feszültség-skála kezdete és nyílása tizedvoltban

    // Mit csinál: egy 90°-os mérőívet rajzol a tárcsára: vékony ív, piros szélső sávok, kis és nagy osztások, számok.
    // Hogyan:     a pozíciók 0..span egészek; a feliratok szövege (lab_min + pozíció) / lab_div. A piros sáv: [0, red_lo_to]
    //             és [red_hi_from, span] (red_lo_to = 0, illetve red_hi_from = span: nincs ilyen sáv). Az óramutatóval
    //             szemben növő skálán (sweep < 0) az LVGL osztásai fordított sorrendűek, ezért a szakaszokat tükrözzük.
    void add_gauge(lv_obj_t* host, int32_t start, int32_t sweep, int32_t span, int32_t big, int32_t fine, int32_t lab_min,
                   int32_t lab_div, int32_t red_lo_to, int32_t red_hi_from) {
        start_deg = start;
        sweep_deg = sweep;
        gmax = span;
        const bool rev = sweep < 0;
        auto section = [&](lv_obj_t* sc, lv_style_t* ind, lv_style_t* items, int32_t a, int32_t b) {
            int32_t x0 = rev ? span - b : a, x1 = rev ? span - a : b;
            lv_scale_section_t* z = lv_scale_add_section(sc);
            lv_scale_set_section_range(sc, z, x0, x1);
            lv_scale_set_section_style_indicator(sc, z, ind);
            if (items) lv_scale_set_section_style_items(sc, z, items);
        };
        const bool lo = red_lo_to > 0, hi = red_hi_from < span;

        add_arc(host, 0, span, 2, g_accent);
        if (lo) add_arc(host, 0, red_lo_to, 8, g_warn);
        if (hi) add_arc(host, red_hi_from, span, 8, g_warn);

        lv_obj_t* f = add_scale(host, fine);
        lv_scale_set_major_tick_every(f, static_cast<uint32_t>(span / fine + 1));  // csak a 0. osztás "nagy"
        lv_obj_set_style_line_width(f, 2, LV_PART_ITEMS);
        lv_obj_set_style_line_color(f, g_dim, LV_PART_ITEMS);
        lv_obj_set_style_length(f, 8, LV_PART_ITEMS);
        lv_obj_set_style_line_width(f, 2, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(f, g_dim, LV_PART_INDICATOR);
        lv_obj_set_style_length(f, 8, LV_PART_INDICATOR);
        if (lo) section(f, &red_minor, &red_minor, 0, red_lo_to);
        if (hi) section(f, &red_minor, &red_minor, red_hi_from, span);

        lv_obj_t* b = add_scale(host, big);
        lv_scale_set_major_tick_every(b, 1);
        lv_scale_set_label_show(b, false);  // a feliratokat az add_label teszi ki
        lv_obj_set_style_line_width(b, 3, LV_PART_INDICATOR);
        lv_obj_set_style_line_color(b, g_text, LV_PART_INDICATOR);
        lv_obj_set_style_length(b, 16, LV_PART_INDICATOR);
        if (lo) section(b, &red_major, nullptr, 0, red_lo_to);
        if (hi) section(b, &red_major, nullptr, red_hi_from, span);

        char t[12];
        for (int32_t p = 0; p <= span; p += big) {
            std::snprintf(t, sizeof t, "%d", static_cast<int>((lab_min + p) / lab_div));
            add_label(host, p, t, &og_font_montserrat_20, ((lo && p <= red_lo_to) || (hi && p >= red_hi_from)) ? g_warn : g_text, 16, 5);
        }
    }

    // Mit csinál: a tárcsa kész képét előállítja a jelenlegi hőmérséklet-mértékegységgel.
    void render_face() {
        lv_obj_t* host = begin_face();
        if (v24) add_gauge(host, kBatStart, kBatSweep, b_span, 10, 5, b_min, 10, 5, 65);   // 23,0..30,0 V; piros <= 23,5 és >= 29,5
        else add_gauge(host, kBatStart, kBatSweep, b_span, 10, 2, b_min, 10, 10, 50);      // 10,0..16,0 V; piros <= 11,0 és >= 15,0
        if (fahrenheit) add_gauge(host, kTempStart, kTempSweep, t_span, 30, 10, t_min, 1, 0, 80);  // 140..260 °F; piros 220-tól
        else add_gauge(host, kTempStart, kTempSweep, t_span, 15, 5, t_min, 1, 0, 45);              // 60..120 °C; piros 105-től
        end_face(host);
        rendered = true;
        drawn2 = -1;
    }

    // Mit csinál: beállítja a hőmérséklet mértékegységét; csak változáskor generálja újra a tárcsát.
    // Hogyan:     °C: 60..120 (piros 105-től); °F: 140..260 (piros 220-tól).
    void set_scale(bool use_fahrenheit) {
        if (rendered && use_fahrenheit == fahrenheit) return;
        fahrenheit = use_fahrenheit;
        t_min = fahrenheit ? 140 : 60;
        t_span = fahrenheit ? 120 : 60;
        render_face();
    }

    // Mit csinál: beállítja, hogy a rendszerfeszültséget a mérés (Auto) vagy a felhasználó (12 V / 24 V) adja.
    // Hogyan:     kényszerített módban azonnal átáll a skála; Auto módban a skála marad, amíg a mért feszültség át nem vált.
    void set_voltage_mode(SystemVoltage m) {
        mode = m;
        if (m != SystemVoltage::Auto) set_system_24v(m == SystemVoltage::V24);
    }

    // Mit csinál: a feszültség-skálát a rendszerfeszültséghez igazítja; csak változáskor rajzolja újra a tárcsát.
    void set_system_24v(bool on) {
        if (on == v24 && rendered) return;
        v24 = on;
        b_min = v24 ? 230 : 100;
        b_span = v24 ? 70 : 60;
        if (rendered) render_face();  // az első rajzolást a set_scale végzi
    }

    void build() {
        build_base();
        needle2 = lv_image_create(scr);
        if (needle_buf) lv_image_set_src(needle2, needle_buf);
        lv_obj_set_pos(needle2, kCenter - kImgW / 2, kCenter - kImgPivotY);
        lv_image_set_pivot(needle2, kImgW / 2, kImgPivotY);
        lv_obj_set_style_image_recolor(needle2, g_needle, 0);
        lv_obj_set_style_image_recolor_opa(needle2, LV_OPA_COVER, 0);
        lv_obj_remove_flag(needle2, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_move_to_index(needle2, lv_obj_get_index(needle) + 1);  // az agy a két tű fölött marad
        lv_image_set_rotation(needle, rotation_of(0, b_span, kBatStart, kBatSweep));
        lv_image_set_rotation(needle2, rotation_of(0, t_span, kTempStart, kTempSweep));

        // A számértékek a tárcsa két oldalán, a tűk söprési sávján kívül: bal oldalt az akkumulátor, jobb oldalt a hűtővíz.
        value2 = make_label(scr, &og_font_montserrat_28, g_text, LV_ALIGN_CENTER, 96, 10, "--");
        for (lv_obj_t* l : {value, value2}) {
            lv_obj_set_width(l, 112);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        }
        lv_obj_align(value, LV_ALIGN_CENTER, -96, 10);
        lv_obj_align(value2, LV_ALIGN_CENTER, 96, 10);
        // Ikonok a feliratok fölött: autó-akkumulátor (két sarus test + és - jel) és hőmérő (cső, gömb, mercuri, osztások).
        icon_bat = make_icon(32, 32, [](float x, float y) {
            const bool body = in_rrect(x, y, 1, 10, 31, 29, 3) && !in_rrect(x, y, 3.4f, 12.4f, 28.6f, 26.6f, 1.2f);
            const bool terminals = in_rrect(x, y, 5, 4.5f, 10.5f, 10.5f, 1) || in_rrect(x, y, 21.5f, 4.5f, 27, 10.5f, 1);
            const bool plus = (x >= 6.5f && x <= 12.5f && y >= 18.2f && y <= 20.4f) || (x >= 8.6f && x <= 10.4f && y >= 16.2f && y <= 22.4f);
            const bool minus = x >= 19.5f && x <= 25.5f && y >= 18.2f && y <= 20.4f;
            return body || terminals || plus || minus;
        });
        icon_temp = make_icon(32, 32, [](float x, float y) {
            const bool outer = in_rrect(x, y, 11, 1, 21, 24, 5) || in_circle(x, y, 16, 25, 7.2f);
            const bool inner = in_rrect(x, y, 13.4f, 3.4f, 18.6f, 24, 2.6f) || in_circle(x, y, 16, 25, 4.8f);
            const bool fluid = (x >= 14.5f && x <= 17.5f && y >= 11 && y <= 25) || in_circle(x, y, 16, 25, 3.5f);
            const bool ticks = x >= 23 && x <= 28 && ((y >= 5 && y <= 7) || (y >= 10 && y <= 12) || (y >= 15 && y <= 17));
            return (outer && !inner) || fluid || ticks;
        });
        add_icon(icon_bat, -96, -52);
        add_icon(icon_temp, 96, -52);
        make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, -96, -20, "AKKU");
        make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, 96, -20, "HŰTŐVÍZ");
        lv_obj_align(nodata, LV_ALIGN_CENTER, 0, 48);
    }

    // Mit csinál: egy tűt a cél felé simítva mozgat, és csak a forgatás (0,1°) változásakor rajzoltat újra.
    void step_needle(lv_obj_t* n, float& sh, int32_t& dr, float target, int32_t span, int32_t start, int32_t sweep) {
        sh += (target - sh) * kSmooth;
        if (std::abs(target - sh) < static_cast<float>(span) / (static_cast<float>(std::abs(sweep)) * 10.f) * 0.5f) sh = target;
        const int32_t rot = rotation_of(sh, span, start, sweep);
        if (rot != dr) {
            lv_image_set_rotation(n, rot);
            dr = rot;
        }
    }

    void update(const DashValues& v) {
        auto ok = [&](uint16_t f) { return v.fresh && (v.flags & f); };
        const bool ok_v = ok(kFlagVbat), ok_c = ok(kFlagCoolant);
        if (mode == SystemVoltage::Auto && ok_v) {  // a skála a mért feszültséghez igazodik
            if (!v24 && v.vbat_v > kV24On) set_system_24v(true);
            else if (v24 && v.vbat_v < kV24Off) set_system_24v(false);
        }
        const float temp = fahrenheit ? v.coolant_c * 1.8f + 32.f : v.coolant_c;
        step_needle(needle, shown, drawn, ok_v ? std::clamp(v.vbat_v * 10.f - static_cast<float>(b_min), 0.f, static_cast<float>(b_span)) : 0.f, b_span,
                    kBatStart, kBatSweep);
        step_needle(needle2, shown2, drawn2, ok_c ? std::clamp(temp - static_cast<float>(t_min), 0.f, static_cast<float>(t_span)) : 0.f,
                    t_span, kTempStart, kTempSweep);
        char b[24];
        if (ok_v) fmt_dec(b, sizeof b, "%.1f V", v.vbat_v);
        else std::snprintf(b, sizeof b, "--");
        set_text(value, b);
        if (ok_c) std::snprintf(b, sizeof b, fahrenheit ? "%d °F" : "%d °C", static_cast<int>(temp));
        else std::snprintf(b, sizeof b, "--");
        set_text(value2, b);
        show_nodata(v.fresh);
    }
};

// (7) Fokozatközpontú képernyő: középen a nagy fokozatszám (vagy "N"), körülötte zónás fordulatív és egy sáv jelző.
// Hogyan:     az ív három zónára bomlik: zöld a max. fordulat alsó 30%-áig (500-ra, 8000 fölött 1000-re kerekítve), narancs a
//             zöld teteje és a redline között, piros a redline-tól; a zónák színe fix (a jelentésük miatt nem a palettából jön).
//             A skálakép egyszer képbe készül (mint a mutatós műszereknél); a jelző két kis A8 kép (fekete szegély és fehér
//             sáv), amelyet az LVGL a képernyő középpontja körül forgat. A nagy számjegyek külön betűkészletből vannak.
// Miért:      a fokozat és a fordulat együtt mutatja, mikor érdemes váltani: a zöld a takarékos tartomány, a piros a túlpörgetés.
struct GearView : NeedleGaugeBase {
    static constexpr int32_t kBarW = 16, kBarH = 44;        // a jelző képe (a sáv a skálaívre merőleges)
    static constexpr int32_t kBarPivotY = kDiameter / 2 + 6;  // a forgáspont a kép tetejétől a képernyő középpontjáig (a kép alatt)
    static constexpr int32_t kArcW = 26;                    // az ív vastagsága
    lv_obj_t* digit = nullptr;       // a nagy fokozatszám
    lv_obj_t* rpm_value = nullptr;   // a fordulatszám számmal
    lv_obj_t* caption = nullptr;     // "rpm" vagy "Tanulás n/N"
    lv_obj_t* bar_dark = nullptr;
    lv_obj_t* bar_light = nullptr;
    lv_draw_buf_t* bar_dark_buf = nullptr;
    lv_draw_buf_t* bar_light_buf = nullptr;
    int32_t rpm_green = 2500, rpm_red = 6500;
    bool rendered = false;

    // Mit csinál: a zöld zóna teteje: a max. fordulat 30%-a, 500-ra (8000 fölött 1000-re) kerekítve, legalább 500.
    static int32_t green_top_of(int32_t max_rpm) {
        const int32_t step = max_rpm > 8000 ? 1000 : 500;
        return std::max<int32_t>(step, (max_rpm * 3 / 10 + step / 2) / step * step);
    }

    // Mit csinál: a skálakép (zónás ív és az ezres számok) előállítása a jelenlegi skálavéggel és redline-nal.
    void render_face() {
        lv_obj_t* host = begin_face();
        start_deg = 135;
        sweep_deg = 270;
        const lv_color_t green = lv_color_hex(0x22B14C), orange = lv_color_hex(0xFF8A1F);
        const int32_t gt = std::min(rpm_green, rpm_red);
        add_arc(host, 0, gt, kArcW, green);
        if (rpm_red > gt) add_arc(host, gt, rpm_red, kArcW, orange);
        if (rpm_red < gmax) add_arc(host, rpm_red, gmax, kArcW, g_warn);
        char b[12];
        for (int32_t v = 0; v <= gmax / 1000 * 1000; v += 1000) {
            std::snprintf(b, sizeof b, "%d", static_cast<int>(v / 1000));
            add_label(host, v, b, &lv_font_montserrat_14, g_dim, kArcW, 4);  // a számok az ív belső oldalán
        }
        end_face(host);
        rendered = true;
    }

    // Mit csinál: új skálavég és redline (beállítás módosításakor): újragenerálja az ívet.
    void set_scale(int32_t max, int32_t redline) {
        const int32_t g = std::max<int32_t>(1000, max / 100 * 100);
        const int32_t red = std::min<int32_t>(redline, g);
        const int32_t green = green_top_of(g);
        if (rendered && g == gmax && red == rpm_red && green == rpm_green) return;
        gmax = g;
        rpm_red = red;
        rpm_green = green;
        render_face();
    }

    void build() {
        build_face_part();
        gmax = 8000;
        // A jelző: fekete szegély (szélesebb, hosszabb) és fehér sáv; a forgáspont a képernyő középpontja (a képen kívül).
        bar_dark_buf = make_icon(kBarW, kBarH, [](float x, float y) { return in_rrect(x, y, 1, 0, 15, 44, 3); });
        bar_light_buf = make_icon(kBarW, kBarH, [](float x, float y) { return in_rrect(x, y, 4, 3, 12, 41, 2); });
        for (lv_obj_t** o : {&bar_dark, &bar_light}) {
            *o = lv_image_create(scr);
            lv_image_set_src(*o, o == &bar_dark ? bar_dark_buf : bar_light_buf);
            lv_obj_set_pos(*o, kCenter - kBarW / 2, kCenter - kBarPivotY);
            lv_image_set_pivot(*o, kBarW / 2, kBarPivotY);
            lv_obj_set_style_image_recolor(*o, o == &bar_dark ? lv_color_black() : lv_color_white(), 0);
            lv_obj_set_style_image_recolor_opa(*o, LV_OPA_COVER, 0);
            lv_obj_remove_flag(*o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
            lv_image_set_rotation(*o, rotation_of(0, gmax, 135, 270));
        }
        digit = make_label(scr, &og_font_digits_140, g_text, LV_ALIGN_CENTER, 0, -16, "-");
        rpm_value = make_label(scr, &og_font_montserrat_28, g_text, LV_ALIGN_CENTER, 0, 82, "--");
        caption = make_label(scr, &og_font_montserrat_20, g_dim, LV_ALIGN_CENTER, 0, 112, "rpm");
        nodata = make_label(scr, &og_font_montserrat_20, g_warn, LV_ALIGN_CENTER, 0, 138, "NINCS ADAT");
        for (lv_obj_t* l : {digit, rpm_value, caption, nodata}) {
            lv_obj_set_width(l, 200);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        }
        lv_obj_align(digit, LV_ALIGN_CENTER, 0, -16);
        lv_obj_align(rpm_value, LV_ALIGN_CENTER, 0, 82);
        lv_obj_align(caption, LV_ALIGN_CENTER, 0, 112);
        lv_obj_align(nodata, LV_ALIGN_CENTER, 0, 138);
    }

    // Mit csinál: a jelzőt a fordulat felé simítva mozgatja (csak 0,1°-os változásra rajzol újra).
    void move_bar(float target) {
        shown += (target - shown) * kSmooth;
        if (std::abs(target - shown) < static_cast<float>(gmax) / 2700.f * 0.5f) shown = target;
        const int32_t rot = rotation_of(shown, gmax, 135, 270);
        if (rot != drawn) {
            lv_image_set_rotation(bar_dark, rot);
            lv_image_set_rotation(bar_light, rot);
            drawn = rot;
        }
    }

    void update(const DashValues& v) {
        const bool ok_rpm = v.fresh && (v.flags & kFlagRpm);
        move_bar(ok_rpm ? std::min<float>(v.rpm, static_cast<float>(gmax)) : 0.f);
        char b[24];
        if (ok_rpm) std::snprintf(b, sizeof b, "%d", static_cast<int>((v.rpm + 5.f) / 10.f) * 10);
        else std::snprintf(b, sizeof b, "--");
        set_text(rpm_value, b);
        const bool ready = g_gear_ready.load(std::memory_order_relaxed);
        const int gear = g_gear_now.load(std::memory_order_relaxed);
        const bool neutral = g_gear_neutral.load(std::memory_order_relaxed);
        if (!v.fresh) std::snprintf(b, sizeof b, "-");
        else if (neutral) std::snprintf(b, sizeof b, "N");  // állás (tanulás közben is) vagy tartós gurulás üresben
        else if (!ready) std::snprintf(b, sizeof b, "-");
        else if (gear > 0) std::snprintf(b, sizeof b, "%d", gear);
        else std::snprintf(b, sizeof b, "-");
        set_text(digit, b);
        if (ready || !v.fresh) std::snprintf(b, sizeof b, "rpm");
        else std::snprintf(b, sizeof b, "Tanulás %d/%d", g_gear_known.load(std::memory_order_relaxed), g_gear_total.load(std::memory_order_relaxed));
        set_text(caption, b);
        show_nodata(v.fresh);
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
constexpr int kNeedleTachId = 4;
constexpr int kNeedleSpeedId = 5;
constexpr int kGearViewId = 7;  // fokozatközpontú képernyő
constexpr int kNeedleDualId = 6;  // kettős mutatós mérő: akkumulátorfeszültség és hűtővíz

MainView g_main;
DetailView g_detail;
LinkView g_linkv;
NeedleTachView g_needle_tach;
NeedleSpeedView g_needle_speed;
NeedleDualView g_needle_dual;
GearView g_gear_view;
lv_obj_t* g_screens[kScreenIds] = {};  // képernyőazonosító szerint: 0 fő, 1 részletek, 2 kapcsolat, 3 zene, 4 mutatós fordulatszámmérő, 5 mutatós sebességmérő, 6 kettős mérő (akkumulátor és hűtővíz), 7 fokozatközpontú képernyő
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
    g_needle_tach.update(v);
    g_needle_speed.update(v);
    g_needle_dual.update(v);
    g_gear_view.update(v);
#if defined(CONFIG_OG_DISP_MUSIC)
    g_music.update(ble_ams_info(), now_ms());
#endif
}

// Mit csinál: megmondja, részt vesz-e a képernyő a lapozásban a beállítások szerint.
// Miért:      a Részletek és a Kapcsolat a debug módhoz tartozik, a zene a zene-kapcsolóhoz.
bool screen_enabled(int id) {
    if (!g_screens[id]) return false;
    if (!((g_cfg.screen_mask >> id) & 1)) return false;  // a felhasználó kivette a lapozásból
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
    g_settings_view.open(g_cfg);  // a képernyő itt épül fel (lusta felépítés: nyitva tartása nélkül nem foglal LVGL-memóriát)
    lv_screen_load_anim(g_settings_view.scr, LV_SCREEN_LOAD_ANIM_MOVE_TOP, kSwitchAnimMs, 0, false);
    mem_probe("beállítások megnyitva");
}

// Mit csinál: a módosított beállításokat azonnal érvényesíti (skála, fényerő, lapozás, debug kijelzés, lista).
void apply_live() {
    g_main.set_scale(g_cfg.max_rpm, g_cfg.redline_rpm);
    g_needle_tach.set_scale(g_cfg.max_rpm, g_cfg.redline_rpm);
    g_needle_speed.set_scale(g_cfg.max_speed_kmh, g_cfg.speed_unit == SpeedUnit::Mph);
    g_needle_dual.set_voltage_mode(g_cfg.system_voltage);
    g_needle_dual.set_scale(g_cfg.temp_unit == TempUnit::Fahrenheit);
    g_gear_view.set_scale(g_cfg.max_rpm, g_cfg.redline_rpm);
    gear_apply_config();
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
    // auto_del: az átmenet végén az LVGL törli a beállítások képernyőjét (a kupac felszabadul, a következő megnyitás újraépíti).
    lv_screen_load_anim(g_screens[g_active[g_pos]], LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, kSwitchAnimMs, 0, true);
}

// Mit csinál: a beállítások fő listájának jobbra húzása: a módosításokat elveti, és visszalép az előző képernyőre.
void on_settings_cancel() {
    g_pause_until = now_ms() + kTouchPauseMs;
    lv_screen_load_anim(g_screens[g_active[g_pos]], LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, kSwitchAnimMs, 0, true);
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
    gear_persist(now_ms(), true);
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

#if defined(CONFIG_OG_DISP_BENCH)
void log_prof(const char* what, const LvglProf& p);
}  // namespace
extern "C" void og_prof_enable(bool on);
extern "C" void og_prof_dump(const char* what);
namespace {
std::atomic<bool> g_bench_busy{false};  // átmenet-mérés közben a másodpercenkénti kiírás nem nullázhatja a mérőt
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
            DashValues dv;
            {
                std::lock_guard<std::mutex> lock(g_mx);
                fresh = g_model.on_packet(p.data, p.len, p.rssi, p.rx_ms);
                stopped = g_model.engine_stopped();
                stopped_ms = g_model.stopped_ms();
                new_vin = g_model.take_new_vin(vin);
                if (fresh) dv = g_model.values(p.rx_ms);
            }
            if (fresh) gear_feed(dv);
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
        gear_persist(now, false);
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

#if defined(CONFIG_OG_DISP_BENCH)
        if (!g_bench_busy.load()) log_prof("1 s", lvgl_take_prof());
#endif
        const EspNowLink::Stats ls = g_link.stats();
        ESP_LOGI(TAG,
                 "%lu csomag/s, RSSI %d dBm | összes %lu, elveszett %lu (%.2f %%, 60 s: %.2f %%), elavult %lu, újraindulás %lu, "
                 "érvénytelen %lu, sor tele %lu, seq %u | adat %s, %lu 1/perc, %d km/h | FPS %lu, belső RAM %u kB | VIN %s | fokozat %d%s (%d/%d)",
                 (unsigned long)l.per_sec, l.rssi_dbm, (unsigned long)l.received, (unsigned long)l.lost,
                 l.loss_ratio * 100.0, l.loss_ratio_window * 100.0, (unsigned long)l.stale, (unsigned long)l.resyncs, (unsigned long)l.invalid,
                 (unsigned long)ls.rx_dropped, l.last_seq, v.fresh ? "friss" : "NINCS", (unsigned long)(v.rpm + 0.5f), (int)(v.speed_kmh + 0.5f),
                 (unsigned long)lvgl_take_frames(), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), cur_vin[0] ? cur_vin : "--", g_gear_now.load(), g_gear_neutral.load() ? " N" : "", g_gear_known.load(), g_gear_total.load());
    }
}

#if defined(CONFIG_OG_DISP_BENCH)
// Mit csinál: a mért időket egy sorban kiírja (képkockák, átlag/max, várakozás, flush, rajzolás, tényleges FPS).
void log_prof(const char* what, const LvglProf& p) {
    if (!p.frames) {
        ESP_LOGI(TAG, "BENCH %s: nem volt rajzolás", what);
        return;
    }
    const double n = p.frames;
    const double frame = p.frame_us / n / 1000.0, wait = p.wait_us / n / 1000.0, flush = p.flush_us / n / 1000.0;
    const double period = p.frames > 1 ? p.period_us / (n - 1) / 1000.0 : 0.0;
    ESP_LOGI(TAG,
             "BENCH %s: %lu kép, %.1f sáv/kép | idő/kép átl %.1f ms (max %.1f) | várakozás %.1f ms, flush-hívás %.1f ms, rajzolás %.1f ms | "
             "periódus %.1f ms = %.0f FPS | lassú(>33ms) %lu",
             what, (unsigned long)p.frames, p.chunks / n, frame, p.max_frame_us / 1000.0, wait, flush, frame - wait - flush, period,
             period > 0 ? 1000.0 / period : 0.0, (unsigned long)p.slow_frames);
}

// Mit csinál: a logó lejárta után körbe lapozza a képernyőket, és minden átmenetnél méri a rajzolást.
// Hogyan:     az átmenet 400 ms; a mérés a váltás előtt nullázódik, 900 ms múlva olvassa le (üresjárat is benne van, ezért
//             a periódus és a kép-adatok az animáció alatti képkockákra vonatkoznak: csak rajzolt képkockákat számol).
void bench_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(9000));
#if defined(CONFIG_LV_USE_PROFILER)
    constexpr int kRounds = 4;  // az utolsó kör az LVGL-profilozóval (a mért idők ilyenkor torzultak)
#else
    constexpr int kRounds = 3;
#endif
    for (int round = 0; round < kRounds; ++round) {
        const bool prof = round == 3;
        for (int i = 0; i < g_active_n; ++i) {
            const int from = g_active[g_pos];
            g_bench_busy = true;
            lvgl_take_prof();
            if (prof) og_prof_enable(true);
            if (lvgl_port_lock(1000)) {
                show_next(+1);
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(kSwitchAnimMs + 500));
            char name[24];
            snprintf(name, sizeof name, "%d->%d", from, g_active[g_pos]);
            log_prof(name, lvgl_take_prof());
            if (prof) og_prof_dump(name);
            g_bench_busy = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    // Álló képernyők: 6 s-ig egy helyben, hogy a mozgó tű/ív idleje is látsszon (az átmenetek már lecsengtek).
    for (const int id : {0, kNeedleTachId, kNeedleSpeedId, kNeedleDualId, kGearViewId}) {
        for (int k = 0; k < g_active_n; ++k) {
            if (g_active[k] != id) continue;
            g_bench_busy = true;
            if (lvgl_port_lock(1000)) {
                load_pos(k, LV_SCREEN_LOAD_ANIM_NONE);
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(1500));
            lvgl_take_prof();
            vTaskDelay(pdMS_TO_TICKS(6000));
            char name[24];
            snprintf(name, sizeof name, "all %d", id);
            log_prof(name, lvgl_take_prof());
            g_bench_busy = false;
        }
    }
    lv_mem_monitor_t mon;
    if (lvgl_port_lock(1000)) {
        lv_mem_monitor(&mon);
        lvgl_port_unlock();
        ESP_LOGI(TAG, "BENCH LVGL-kupac: %lu B összesen, szabad %lu B, legnagyobb használat %lu %%, töredezettség %u %%",
                 (unsigned long)mon.total_size, (unsigned long)mon.free_size, (unsigned long)(mon.max_used * 100ull / mon.total_size), (unsigned)mon.frag_pct);
    }
    ESP_LOGI(TAG, "BENCH belső RAM szabad: %u B, legnagyobb blokk %u B", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "BENCH kész");
    vTaskDelete(nullptr);
}
#endif

#if defined(CONFIG_OG_DISP_BOOT_LOGO)
lv_obj_t* g_logo_scr = nullptr;

lv_obj_t* g_logo_img = nullptr;

// Mit csinál: a logó átlátszóságát állítja az animációból (a képre közvetlenül, nem külön rétegre).
// Miért:      a teljes képernyő áttűnése (FADE_ON) két teljes képernyőt számol pixelenként; a fekete háttéren
//             a logó átlátszósága ugyanazt a látványt adja a kép kis területén.
void logo_opa_cb(void* img, int32_t v) { lv_obj_set_style_opa(static_cast<lv_obj_t*>(img), static_cast<lv_opa_t>(v), 0); }

// Mit csinál: a logó eltűnése után azonnal betölti a műszerfalat, és törli a logó képernyőt.
void logo_faded_out(lv_anim_t*) { lv_screen_load_anim(g_main.scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true); }

// Mit csinál: a logó időzítőjének lejártakor elhalványítja a logót, majd a műszerfalra vált.
// Miért:      a logó csak indulásnál kell; a felszabadított képernyő nem foglal memóriát.
void logo_done(lv_timer_t* t) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_logo_img);
    lv_anim_set_exec_cb(&a, logo_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, kLogoFadeOutMs);
    lv_anim_set_completed_cb(&a, logo_faded_out);
    lv_anim_start(&a);
    lv_timer_delete(t);
}

// Mit csinál: fekete képernyőt épít a középre tett fehér logóval, és betölti felúszással.
// Hogyan:     a kép A8 maszk, a színét az image_recolor adja; a műszerfalra váltást egyszeri időzítő indítja.
// Miért:      a 4 másodperc alatt a két egység párosodik és a BLE is elindul; az automatikus lapozás
//             addig nem kezd el váltani.
void show_boot_logo(const lv_image_dsc_t* logo) {
    g_logo_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_logo_scr, g_bg, 0);
    lv_obj_set_style_bg_opa(g_logo_scr, LV_OPA_COVER, 0);
    lv_obj_t* img = lv_image_create(g_logo_scr);
    g_logo_img = img;
    lv_image_set_src(img, logo);
    lv_obj_set_style_image_recolor(img, g_text, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
    lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);
    lv_screen_load(g_logo_scr);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, logo_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, kLogoFadeInMs);
    lv_anim_start(&a);
    lv_timer_create(logo_done, kLogoFadeInMs + kLogoHoldMs, nullptr);
    g_pause_until = now_ms() + kLogoFadeInMs + kLogoHoldMs + kLogoFadeOutMs + g_cfg.switch_period_s * 1000u;
}
#endif

// Mit csinál: az indulás utáni első LVGL-ütemben előállítja a mutatós fordulatszámmérő skálájának képét.
// Miért:      a rajzolást az LVGL-feladat végzi (a main feladat verme kicsi a képrajzoláshoz).
void initial_scale_tick(lv_timer_t*) {
    mem_probe("skálarajzolás előtt");
    g_needle_tach.set_scale(g_cfg.max_rpm, g_cfg.redline_rpm);
    mem_probe("mutatós fordulatszám skála");
    g_needle_speed.set_scale(g_cfg.max_speed_kmh, g_cfg.speed_unit == SpeedUnit::Mph);
    mem_probe("mutatós sebesség skála");
    g_needle_dual.set_voltage_mode(g_cfg.system_voltage);
    g_needle_dual.set_scale(g_cfg.temp_unit == TempUnit::Fahrenheit);
    mem_probe("kettős mérő skála");
    g_gear_view.set_scale(g_cfg.max_rpm, g_cfg.redline_rpm);
    mem_probe("fokozat-képernyő skála");
#if defined(CONFIG_OG_DISP_MEMPROF)
    lv_timer_create(mem_probe_timer, 20000, nullptr);
#endif
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
    const bool cfg_loaded = settings_load(g_cfg);  // az ESP-NOW indítása már előkészítette az NVS-t
    apply_palette(g_cfg);
    g_backlight_pct = g_cfg.brightness_pct;
    if (gear_state_load(g_gear)) ESP_LOGI(TAG, "Tanult fokozatok betöltve: %d fokozat, %lu minta", g_gear.learned_gears(), (unsigned long)g_gear.samples());
    gear_apply_config();
    ESP_LOGI(TAG, "Beállítások: %s, paletta: %s", cfg_loaded ? "mentett" : "alapértelmezett", palette_get(g_cfg.palette).name);
    g_adc_ok = g_adc.init(board::kPinBattery);
    lv_display_t* disp = lvgl_start(lcd, swap_bytes);
    if (!disp) return;
#if defined(CONFIG_OG_DISP_MEMPROF)
    if (lvgl_port_lock(0)) { mem_probe("LVGL indult"); lvgl_port_unlock(); }
#endif
    if (touch && !lvgl_add_touch(disp, *touch)) ESP_LOGW(TAG, "Érintő nélkül fut: csak automatikus váltás");

#if defined(CONFIG_OG_DISP_BOOT_LOGO)
    // Az indító logó az utoljára látott VIN márkájáé: a VIN a CAN-oldalról később érkezik, mint a kijelző indulása,
    // ezért nem várjuk meg. Ha nincs mentett VIN vagy a márkához nincs logó, a logó lépés kimarad.
    const lv_image_dsc_t* boot_logo = nullptr;
    {
        char saved[18];
        if (last_vin_load(saved)) boot_logo = logo_for_brand(vin_brand_id(saved));
        ESP_LOGI(TAG, "Indító logó: %s", boot_logo ? vin_brand_name(vin_brand_id(saved)) : "nincs (kimarad)");
    }
#endif
    if (lvgl_port_lock(0)) {
        mem_probe("build előtt");
        g_main.build();
        mem_probe("fő képernyő");
        g_detail.build();
        mem_probe("részletek képernyő");
        g_linkv.build();
        mem_probe("kapcsolat képernyő");
        g_screens[0] = g_main.scr;
        g_screens[1] = g_detail.scr;
        g_screens[2] = g_linkv.scr;
        g_needle_tach.build();
        g_screens[kNeedleTachId] = g_needle_tach.scr;
        mem_probe("mutatós fordulatszám (váz)");
        g_needle_speed.build();
        g_screens[kNeedleSpeedId] = g_needle_speed.scr;
        mem_probe("mutatós sebesség (váz)");
        g_needle_dual.build();
        g_screens[kNeedleDualId] = g_needle_dual.scr;
        mem_probe("kettős mérő (váz)");
        g_gear_view.build();
        g_screens[kGearViewId] = g_gear_view.scr;
        mem_probe("fokozat-képernyő (váz)");
#if defined(CONFIG_OG_DISP_MUSIC)
        g_music.build();
        g_screens[kMusicId] = g_music.scr;
        mem_probe("zene képernyő");
#endif
        for (lv_obj_t* scr : g_screens)
            if (scr) lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, nullptr);
        g_settings_view.init(UiColors{g_bg, g_track, g_text, g_dim, g_accent, g_warn}, on_settings_exit, on_settings_cancel);
        mem_probe("beállítások (init, nincs képernyő)");

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
        mem_probe("fő skála (set_scale)");
        // A skála képét az LVGL-feladat rajzolja (a main feladat verme kicsi hozzá): egyszeri időzítő az első ütemben.
        lv_timer_set_repeat_count(lv_timer_create(initial_scale_tick, 1, nullptr), 1);
        rebuild_active();
        set_perf_visible(g_cfg.debug_mode);
#if defined(CONFIG_OG_DISP_BOOT_LOGO)
        if (boot_logo && !g_cfg.skip_logo) show_boot_logo(boot_logo); else lv_screen_load(g_main.scr);
#else
        lv_screen_load(g_main.scr);
#endif
        mem_probe("képernyő betöltve");
        lv_timer_create(ui_tick, kUiPeriodMs, nullptr);
        g_switch_timer = lv_timer_create(switch_tick, g_cfg.switch_period_s * 1000u, nullptr);
        lvgl_port_unlock();
    }

#if defined(CONFIG_OG_DISP_MUSIC)
    // A BLE (iOS zene) mindig kész: a NimBLE host a PSRAM-ban fut (H-34), így belefér a belső RAM-ba.
    mem_probe_locked("BLE indítás előtt");
    if (g_cfg.music_enabled && !ble_ams_start("OpenGauge")) ESP_LOGE(TAG, "A BLE nem indult el");
    mem_probe_locked("BLE elindult");
#endif

    // A kapcsolatfeladat a 0. magon (az LVGL az 1. magon fut).
    xTaskCreatePinnedToCore(link_task, "og_link", 6144, nullptr, 5, nullptr, 0);  // az NVS-mentés (fokozatok) miatt nagyobb verem
    mem_probe_locked("kapcsolatfeladat indult");
#if defined(CONFIG_OG_DISP_BENCH)
    xTaskCreatePinnedToCore(bench_task, "og_bench", 4096, nullptr, 3, nullptr, 0);
#endif
    ESP_LOGI(TAG, "Műszerfal fut: 3 képernyő, váltás %lu ms-onként", (unsigned long)(g_cfg.switch_period_s * 1000u));
}

}  // namespace og
