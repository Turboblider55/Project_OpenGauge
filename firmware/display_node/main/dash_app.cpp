// Kijelzőegység – műszerfal a valós telemetriával (S-07, S-08, S-10 első változata).
//
// Mit csinál: az ESP-NOW-n érkező adatokat három LVGL-képernyőn mutatja, amelyek 5 s-onként
//             animálva váltanak: (1) fő műszer: fordulatszám-ív, fordulatszám és sebesség;
//             (2) részletek: hűtővíz, fojtószelep, feszültség, MIL és hibakódok; (3) kapcsolat:
//             csomag/s, veszteség, térerő, sorszám, az adat kora. 500 ms-nál régebbi adatnál
//             "NINCS ADAT" jelzés. Másodpercenként egy naplósor: kapcsolatstatisztika és FPS.
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

#include <cstdio>
#include <cstring>
#include <mutex>

#include "dash_model.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_setup.h"
#include "sdkconfig.h"

namespace og {

namespace {

const char* TAG = "og_dash";

constexpr uint32_t kUiPeriodMs = 16;       // a nézetek frissítése (~60 Hz)
constexpr uint32_t kSwitchPeriodMs = 5000; // automatikus képernyőváltás
constexpr uint32_t kSwitchAnimMs = 400;
constexpr int32_t kRpmMax = 8000;
constexpr int32_t kRpmRed = 6000;

std::mutex g_mx;      // a g_model-t védi (0. mag: írás, 1. mag: olvasás)
DashModel g_model;
EspNowLink g_link;

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

// ------------------------------------------------------------------------------- nézetek

// (1) Fő műszer: 270°-os fordulatszám-ív, középen a fordulatszám, alatta a sebesség.
struct MainView {
    lv_obj_t* scr;
    lv_obj_t* arc;
    lv_obj_t* rpm;
    lv_obj_t* speed;
    lv_obj_t* nodata;
    bool red = false;

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
    }

    void update(const DashValues& v) {
        char b[24];
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

// (2) Részletek: hűtővíz, fojtószelep, feszültség, MIL és hibakódok.
struct DetailView {
    lv_obj_t* scr;
    Row coolant, throttle, vbat, mil;
    bool mil_red = false;

    void build() {
        scr = make_screen();
        make_label(scr, &lv_font_montserrat_20, kOrange, LV_ALIGN_TOP_MID, 0, 50, "RESZLETEK");
        coolant.build(scr, -75, "VIZ");
        throttle.build(scr, -25, "FOJTO");
        vbat.build(scr, 25, "AKKU");
        mil.build(scr, 75, "MIL");
    }

    void update(const DashValues& v) {
        char b[24];
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

MainView g_main;
DetailView g_detail;
LinkView g_linkv;
lv_obj_t* g_screens[3];
int g_current = 0;

// Mit csinál: kiolvassa a modellt (rövid zár alatt), és frissíti mindhárom nézetet.
// Hogyan:     LVGL-időzítő, az LVGL-feladatban fut (az LVGL zárja már nála van).
// Miért:      a nem látható képernyők frissítése nem jár rajzolással, váltáskor viszont már
//             a friss értékek látszanak.
void ui_tick(lv_timer_t*) {
    DashValues v;
    DashLink l;
    {
        std::lock_guard<std::mutex> lock(g_mx);
        v = g_model.values(now_ms());
        l = g_model.link();
    }
    g_main.update(v);
    g_detail.update(v);
    g_linkv.update(v, l);
}

// Mit csinál: a következő képernyőre vált, balra csúszó animációval.
void switch_tick(lv_timer_t*) {
    g_current = (g_current + 1) % 3;
    lv_screen_load_anim(g_screens[g_current], LV_SCREEN_LOAD_ANIM_MOVE_LEFT, kSwitchAnimMs, 0, false);
}

// ---------------------------------------------------------------------- kapcsolatfeladat

// Mit csinál: fogadja a csomagokat, frissíti a modellt, és másodpercenként naplóz.
// Hogyan:     a 0. magon fut; a zárat csak a modell frissítésének idejére veszi fel.
void link_task(void*) {
    uint32_t next = now_ms() + 1000;
    for (;;) {
        const int32_t rem = static_cast<int32_t>(next - now_ms());
        EspNowLink::Packet p;
        if (rem > 0 && g_link.recv(p, static_cast<uint32_t>(rem))) {
            std::lock_guard<std::mutex> lock(g_mx);
            g_model.on_packet(p.data, p.len, p.rssi, p.rx_ms);
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

void run_dash_app(LcdPanel& lcd, bool swap_bytes) {
    if (!g_link.init(CONFIG_OG_ESPNOW_CHANNEL)) {
        ESP_LOGE(TAG, "Az ESP-NOW nem indult el");
        return;
    }
    if (!lvgl_start(lcd, swap_bytes)) return;

    if (lvgl_port_lock(0)) {
        g_main.build();
        g_detail.build();
        g_linkv.build();
        g_screens[0] = g_main.scr;
        g_screens[1] = g_detail.scr;
        g_screens[2] = g_linkv.scr;
        lv_screen_load(g_main.scr);
        lv_timer_create(ui_tick, kUiPeriodMs, nullptr);
        lv_timer_create(switch_tick, kSwitchPeriodMs, nullptr);
        lvgl_port_unlock();
    }

    // A kapcsolatfeladat a 0. magon (az LVGL az 1. magon fut).
    xTaskCreatePinnedToCore(link_task, "og_link", 4096, nullptr, 5, nullptr, 0);
    ESP_LOGI(TAG, "Műszerfal fut: 3 képernyő, váltás %lu ms-onként", (unsigned long)kSwitchPeriodMs);
}

}  // namespace og
