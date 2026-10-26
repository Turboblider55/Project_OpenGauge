// Kijelzőegység – érintésteszt (S-09 első lépése).
//
// Mit csinál: az érintés helyén pontot mutat a koordinátákkal; a kijelző négy szélén egy-egy
//             célpont (FENT, BAL, JOBB, LENT) zöldre vált, ha a közelében érintés történik.
//             A nyomás kezdetét és végét, valamint a gesztusokat (húzás iránya) naplózza.
// Hogyan:     LVGL-bemenet (lvgl_port_add_touch) a CST816S-ről; a képernyő eseménykezelője
//             kapja a PRESSED / PRESSING / RELEASED / GESTURE eseményeket.
// Miért:      a célpontokból egyszerre látszik, hogy az érintés koordinátái a kijelzőhöz
//             igazodnak-e (90°-os elforgatás, tükrözés); ha nem, a menuconfig tengelycsere- és
//             tükrözésbeállításával javítható, kódmódosítás nélkül.
#include "touch_test.h"

#include <cstdlib>

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

struct Target {
    const char* name;
    lv_align_t align;
    int32_t x, y;   // eltolás az igazításhoz képest
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

const char* dir_name(lv_dir_t d) {
    switch (d) {
        case LV_DIR_LEFT: return "balra";
        case LV_DIR_RIGHT: return "jobbra";
        case LV_DIR_TOP: return "fel";
        case LV_DIR_BOTTOM: return "le";
        default: return "?";
    }
}

// Mit csinál: a pontot az érintés helyére teszi, kiírja a koordinátákat, és megjelöli az
//             eltalált célpontot.
void on_touch(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* in = lv_indev_active();
    if (!in) return;
    if (code == LV_EVENT_GESTURE) {
        ESP_LOGI(TAG, "Gesztus: %s", dir_name(lv_indev_get_gesture_dir(in)));
        return;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_obj_set_pos(g_dot, p.x - 15, p.y - 15);
        lv_obj_set_hidden(g_dot, false);
        lv_label_set_text_fmt(g_coords, "x %d  y %d", static_cast<int>(p.x), static_cast<int>(p.y));
        for (Target& t : g_targets) {
            lv_area_t a;
            lv_obj_get_coords(t.box, &a);
            const int32_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
            if (!t.hit && std::abs(p.x - cx) < kHitRadius && std::abs(p.y - cy) < kHitRadius) {
                t.hit = true;
                lv_obj_set_style_bg_color(t.box, lv_color_hex(0x20C040), 0);
                ESP_LOGI(TAG, "Célpont eltalálva: %s (érintés x %d, y %d; célpont közepe x %ld, y %ld)", t.name,
                         static_cast<int>(p.x), static_cast<int>(p.y), static_cast<long>(cx), static_cast<long>(cy));
            }
        }
    }
    if (code == LV_EVENT_PRESSED) ESP_LOGI(TAG, "Nyomás: x %d, y %d", static_cast<int>(p.x), static_cast<int>(p.y));
    if (code == LV_EVENT_RELEASED) ESP_LOGI(TAG, "Felengedés: x %d, y %d", static_cast<int>(p.x), static_cast<int>(p.y));
}

// Mit csinál: felépíti a tesztképernyőt (az LVGL zárja alatt hívandó).
void build_ui() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_scrollable(scr, false);

    for (Target& t : g_targets) {
        t.box = lv_obj_create(scr);
        lv_obj_set_size(t.box, kTargetSize, kTargetSize);
        lv_obj_align(t.box, t.align, t.x, t.y);
        lv_obj_set_style_radius(t.box, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(t.box, lv_color_hex(0xC03020), 0);
        lv_obj_set_style_border_width(t.box, 0, 0);
        lv_obj_set_clickable(t.box, false);  // az érintés a képernyőhöz menjen
        lv_obj_t* l = lv_label_create(scr);
        lv_label_set_text(l, t.name);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align_to(l, t.box, t.align == LV_ALIGN_BOTTOM_MID ? LV_ALIGN_OUT_TOP_MID : LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
    }

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Erintsd meg\na negy jelet");
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9A9A9A), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, -30);

    g_coords = lv_label_create(scr);
    lv_label_set_text(g_coords, "x --  y --");
    lv_obj_set_style_text_font(g_coords, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_coords, lv_color_white(), 0);
    lv_obj_align(g_coords, LV_ALIGN_CENTER, 0, 30);

    g_dot = lv_obj_create(scr);
    lv_obj_set_size(g_dot, 30, 30);
    lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_dot, lv_color_hex(0xFF8000), 0);
    lv_obj_set_style_border_width(g_dot, 0, 0);
    lv_obj_set_clickable(g_dot, false);
    lv_obj_set_hidden(g_dot, true);

    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(scr, on_touch, LV_EVENT_GESTURE, nullptr);
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
}

}  // namespace og
