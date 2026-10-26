// Kijelzőegység – érintésteszt (S-09 első lépése).
//
// Mit csinál: látható visszajelzést ad az érintésről: narancssárga pont az ujj alatt, a húzás
//             nyoma pontsorként, középen a koordináták, a felismert gesztus nagy felirattal és
//             nyíllal, alul irányonkénti számláló. A kijelző négy szélén egy-egy célpont (FENT,
//             BAL, JOBB, LENT) zöldre vált, ha a közelében érintés történik. A nyomást, a
//             felengedést (a minták számával), a gesztusokat és másodpercenként az FPS-t naplózza.
// Hogyan:     LVGL-bemenet (lvgl_port_add_touch) a CST816-ról; a képernyő eseménykezelője kapja a
//             PRESSED / PRESSING / RELEASED / GESTURE eseményeket. A nyom egy körpuffer: a
//             legrégebbi pont kerül az új helyére, így nem jön létre új objektum húzás közben.
// Miért:      a célpontokból látszik, hogy a koordináták a kijelzőhöz igazodnak-e (tengelycsere,
//             tükrözés: menuconfig); a mintaszámból és a nyomból az, hogy a chip elég sűrűn
//             szolgáltat-e adatot a gesztusfelismeréshez; az FPS-ből pedig, hogy az LVGL rajzol-e.
#include "touch_test.h"

#include <cstdlib>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_setup.h"

namespace og {

namespace {

const char* TAG = "og_touch";

constexpr int32_t kTargetSize = 44;
constexpr int32_t kHitRadius = 50;  // ekkora távolságon belül számít a célpont eltaláltnak
constexpr int kTrailLen = 24;       // a húzásnyom pontjainak száma
constexpr uint32_t kBannerMs = 1200;

const lv_color_t kOrange = lv_color_hex(0xFF8000);
const lv_color_t kGreen = lv_color_hex(0x20C040);
const lv_color_t kGrey = lv_color_hex(0x9A9A9A);

struct Target {
    const char* name;
    lv_align_t align;
    int32_t x, y;  // eltolás az igazításhoz képest
    lv_obj_t* box;
    bool hit;
};

Target g_targets[] = {
    {"FENT", LV_ALIGN_TOP_MID, 0, 20, nullptr, false},
    {"BAL", LV_ALIGN_LEFT_MID, 20, 0, nullptr, false},
    {"JOBB", LV_ALIGN_RIGHT_MID, -20, 0, nullptr, false},
    {"LENT", LV_ALIGN_BOTTOM_MID, 0, -20, nullptr, false},
};

lv_obj_t* g_dot = nullptr;
lv_obj_t* g_coords = nullptr;
lv_obj_t* g_banner = nullptr;
lv_obj_t* g_counts = nullptr;
lv_obj_t* g_trail[kTrailLen];
int g_trail_next = 0;
uint32_t g_samples = 0;     // az aktuális nyomás alatti minták száma
uint32_t g_banner_until = 0;
uint32_t g_dir_count[4] = {0, 0, 0, 0};  // balra, jobbra, fel, le

lv_obj_t* make_circle(lv_obj_t* parent, int32_t size, lv_color_t color) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_clickable(o, false);  // az érintés a képernyőhöz menjen
    lv_obj_set_scrollable(o, false);
    return o;
}

// Mit csinál: a gesztus irányát nagy felirattal és nyíllal mutatja, és növeli a számlálóját.
void show_gesture(lv_dir_t d) {
    const char* text = "?";
    int idx = -1;
    switch (d) {
        case LV_DIR_LEFT: text = LV_SYMBOL_LEFT " BALRA"; idx = 0; break;
        case LV_DIR_RIGHT: text = "JOBBRA " LV_SYMBOL_RIGHT; idx = 1; break;
        case LV_DIR_TOP: text = LV_SYMBOL_UP " FEL"; idx = 2; break;
        case LV_DIR_BOTTOM: text = LV_SYMBOL_DOWN " LE"; idx = 3; break;
        default: break;
    }
    if (idx >= 0) ++g_dir_count[idx];
    lv_label_set_text(g_banner, text);
    lv_obj_set_hidden(g_banner, false);
    g_banner_until = lv_tick_get() + kBannerMs;
    lv_label_set_text_fmt(g_counts, "B %lu  J %lu  F %lu  L %lu", static_cast<unsigned long>(g_dir_count[0]),
                          static_cast<unsigned long>(g_dir_count[1]), static_cast<unsigned long>(g_dir_count[2]),
                          static_cast<unsigned long>(g_dir_count[3]));
    ESP_LOGI(TAG, "Gesztus: %s (eddig B %lu, J %lu, F %lu, L %lu)", text, static_cast<unsigned long>(g_dir_count[0]),
             static_cast<unsigned long>(g_dir_count[1]), static_cast<unsigned long>(g_dir_count[2]),
             static_cast<unsigned long>(g_dir_count[3]));
}

// Mit csinál: az érintés eseményeit kezeli: pont, nyom, koordináták, célpontok, gesztus, napló.
void on_touch(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* in = lv_indev_active();
    if (!in) return;
    if (code == LV_EVENT_GESTURE) {
        show_gesture(lv_indev_get_gesture_dir(in));
        return;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    if (code == LV_EVENT_PRESSED) {
        g_samples = 0;
        for (lv_obj_t* t : g_trail) lv_obj_set_hidden(t, true);
        ESP_LOGI(TAG, "Nyomás: x %d, y %d", static_cast<int>(p.x), static_cast<int>(p.y));
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        ++g_samples;
        lv_obj_set_pos(g_dot, p.x - 15, p.y - 15);
        lv_obj_set_hidden(g_dot, false);
        lv_obj_t* t = g_trail[g_trail_next];
        g_trail_next = (g_trail_next + 1) % kTrailLen;
        lv_obj_set_pos(t, p.x - 4, p.y - 4);
        lv_obj_set_hidden(t, false);
        lv_label_set_text_fmt(g_coords, "x %d  y %d", static_cast<int>(p.x), static_cast<int>(p.y));
        for (Target& tg : g_targets) {
            lv_area_t a;
            lv_obj_get_coords(tg.box, &a);
            const int32_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
            if (!tg.hit && std::abs(p.x - cx) < kHitRadius && std::abs(p.y - cy) < kHitRadius) {
                tg.hit = true;
                lv_obj_set_style_bg_color(tg.box, kGreen, 0);
                ESP_LOGI(TAG, "Célpont eltalálva: %s (érintés x %d, y %d; célpont közepe x %ld, y %ld)", tg.name,
                         static_cast<int>(p.x), static_cast<int>(p.y), static_cast<long>(cx), static_cast<long>(cy));
            }
        }
    }
    if (code == LV_EVENT_RELEASED) {
        lv_obj_set_hidden(g_dot, true);
        ESP_LOGI(TAG, "Felengedés: x %d, y %d (%lu minta)", static_cast<int>(p.x), static_cast<int>(p.y),
                 static_cast<unsigned long>(g_samples));
    }
}

// Mit csinál: a gesztusfeliratot a megjelenítési idő után elrejti.
void banner_tick(lv_timer_t*) {
    if (!lv_obj_is_hidden(g_banner) && static_cast<int32_t>(lv_tick_get() - g_banner_until) >= 0)
        lv_obj_set_hidden(g_banner, true);
}

// Mit csinál: felépíti a tesztképernyőt (az LVGL zárja alatt hívandó).
void build_ui() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    for (Target& t : g_targets) {
        t.box = make_circle(scr, kTargetSize, lv_color_hex(0xC03020));
        lv_obj_align(t.box, t.align, t.x, t.y);
        lv_obj_t* l = lv_label_create(scr);
        lv_label_set_text(l, t.name);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align_to(l, t.box, t.align == LV_ALIGN_BOTTOM_MID ? LV_ALIGN_OUT_TOP_MID : LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    }

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Erintsd meg a jeleket,\nhuzd az ujjad minden iranyba");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, kGrey, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, -70);

    g_coords = lv_label_create(scr);
    lv_label_set_text(g_coords, "x --  y --");
    lv_obj_set_style_text_font(g_coords, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_coords, lv_color_white(), 0);
    lv_obj_align(g_coords, LV_ALIGN_CENTER, 0, -25);

    g_banner = lv_label_create(scr);
    lv_label_set_text(g_banner, "");
    lv_obj_set_style_text_font(g_banner, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_banner, kOrange, 0);
    lv_obj_align(g_banner, LV_ALIGN_CENTER, 0, 25);
    lv_obj_set_hidden(g_banner, true);

    g_counts = lv_label_create(scr);
    lv_label_set_text(g_counts, "B 0  J 0  F 0  L 0");
    lv_obj_set_style_text_color(g_counts, kGrey, 0);
    lv_obj_align(g_counts, LV_ALIGN_CENTER, 0, 75);

    for (lv_obj_t*& t : g_trail) {
        t = make_circle(scr, 8, lv_color_hex(0x4080FF));
        lv_obj_set_hidden(t, true);
    }
    g_dot = make_circle(scr, 30, kOrange);
    lv_obj_set_hidden(g_dot, true);

    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_GESTURE, nullptr);
    lv_timer_create(banner_tick, 100, nullptr);
}

}  // namespace

void run_touch_test(LcdPanel& lcd, bool swap_bytes, TouchPanel& touch) {
    lv_display_t* disp = lvgl_start(lcd, swap_bytes);
    if (!disp) return;
    if (!lvgl_add_touch(disp, touch)) ESP_LOGW(TAG, "Érintő nélkül fut: csak a képernyő látszik");
    if (lvgl_port_lock(0)) {
        build_ui();
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Érintésteszt fut: a célpontok a kijelző szélén vannak (FENT, BAL, JOBB, LENT)");

    // Másodpercenként a kirajzolt képkockák száma: ebből látszik, hogy az LVGL egyáltalán rajzol-e.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "FPS %lu | belső RAM %u kB", static_cast<unsigned long>(lvgl_take_frames()),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    }
}

}  // namespace og
