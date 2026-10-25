// Kijelzőegység – első LVGL-teszt (M3): felirat, ív és mozgó érték FPS-méréssel.
//
// Mit csinál: az LVGL 9-et az esp_lvgl_port komponenssel a már inicializált ST77916 panelre
//             köti, és egy egyszerű műszerképet rajzol: cím, 270°-os ív és nagy számkijelzés,
//             amely 800 és 7000 között oda-vissza animál (mint egy fordulatszámmérő). Másodpercenként
//             naplózza a kirajzolt képkockák számát (FPS) és a szabad memóriát; a képernyő alján az
//             LVGL saját teljesítménymérője (FPS, CPU-terhelés) is látszik.
// Hogyan:     részleges frissítés két, 40 soros DMA-képes pufferrel a belső RAM-ban; az LVGL-feladat
//             az 1. magon fut (a 0. mag később a rádióé lesz). Az LVGL-objektumokat csak a port
//             zárjával (lvgl_port_lock) szabad elérni.
// Miért:      azt méri, hogy a választott pufferelés és QSPI-sebesség mellett megvan-e a G-4 cél
//             (legalább 30 FPS az animációknál), mielőtt a valódi műszerfal elkészül.
#include "lvgl_test.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl_setup.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace og {

namespace {

const char* TAG = "og_lvgl";

lv_obj_t* g_arc = nullptr;
lv_obj_t* g_value = nullptr;

// Mit csinál: az animáció aktuális értékét az ívre és a számkijelzésre írja.
void set_value(void*, int32_t v) {
    lv_arc_set_value(g_arc, v);
    lv_label_set_text_fmt(g_value, "%d", static_cast<int>(v));
}

// Mit csinál: felépíti a tesztképernyőt (az LVGL zárja alatt hívandó).
// Hogyan:     fekete háttér; 300 px-es ív 135°-tól 270°-on át (a kerek kijelző alján nyitott);
//             középen 48-as, alatta 20-as betűméretű felirat; fent cím.
void build_ui() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    g_arc = lv_arc_create(scr);
    lv_obj_set_size(g_arc, 300, 300);
    lv_obj_center(g_arc);
    lv_arc_set_rotation(g_arc, 135);
    lv_arc_set_bg_angles(g_arc, 0, 270);
    lv_arc_set_range(g_arc, 0, 8000);
    lv_obj_remove_style(g_arc, nullptr, LV_PART_KNOB);
    lv_obj_set_clickable(g_arc, false);
    lv_obj_set_style_arc_width(g_arc, 18, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_arc, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_arc, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_arc, lv_color_hex(0xFF8000), LV_PART_INDICATOR);

    g_value = lv_label_create(scr);
    lv_obj_set_style_text_font(g_value, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_value, lv_color_white(), 0);
    lv_label_set_text(g_value, "0");
    lv_obj_align(g_value, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t* unit = lv_label_create(scr);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(0xA0A0A0), 0);
    lv_label_set_text(unit, "1/perc");
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 35);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "OpenGauge");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, set_value);
    lv_anim_set_values(&a, 800, 7000);
    lv_anim_set_duration(&a, 2000);
    lv_anim_set_reverse_duration(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

}  // namespace

void run_lvgl_test(LcdPanel& lcd, bool swap_bytes) {
    if (!lvgl_start(lcd, swap_bytes)) return;
    if (lvgl_port_lock(0)) {
        build_ui();
        lvgl_port_unlock();
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const uint32_t n = lvgl_take_frames();
        ESP_LOGI(TAG, "FPS %lu | szabad belső RAM %u kB, PSRAM %u kB", (unsigned long)n,
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    }
}

}  // namespace og
