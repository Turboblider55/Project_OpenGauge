#include "lvgl_setup.h"

#include <atomic>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "sdkconfig.h"

namespace og {

namespace {

const char* TAG = "og_lvgl";

constexpr int kBufLines = 40;  // egy rajzolópuffer magassága (sor)

std::atomic<uint32_t> g_frames{0};

// Mit csinál: minden teljes képkocka-rajzolás végén növeli a számlálót.
// Miért:      az LV_EVENT_RENDER_READY csak akkor jön, ha ténylegesen volt mit rajzolni, így a
//             számláló a valódi képfrissítési sebességet adja.
void on_render_ready(lv_event_t*) { g_frames.fetch_add(1, std::memory_order_relaxed); }

}  // namespace

lv_display_t* lvgl_start(LcdPanel& lcd, bool swap_bytes) {
    lvgl_port_cfg_t pc = {};
    pc.task_priority = 4;
    pc.task_stack = 7168;
    pc.task_affinity = 1;
    pc.task_max_sleep_ms = 500;
    pc.task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT;
    pc.timer_period_ms = 5;
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init sikertelen");
        return nullptr;
    }

    lvgl_port_display_cfg_t dc = {};
    dc.io_handle = lcd.io();
    dc.panel_handle = lcd.panel();
    dc.buffer_size = LcdPanel::kWidth * kBufLines;
    dc.double_buffer = true;
    dc.hres = LcdPanel::kWidth;
    dc.vres = LcdPanel::kHeight;
    dc.color_format = LV_COLOR_FORMAT_RGB565;
    dc.flags.buff_dma = 1;
    dc.flags.swap_bytes = swap_bytes ? 1 : 0;
    lv_display_t* disp = lvgl_port_add_disp(&dc);
    if (!disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp sikertelen");
        return nullptr;
    }
    if (lvgl_port_lock(0)) {
        lv_display_add_event_cb(disp, on_render_ready, LV_EVENT_RENDER_READY, nullptr);
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "LVGL %d.%d.%d fut: részleges frissítés, 2 x %d soros DMA-puffer, frissítési periódus %d ms",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, kBufLines, CONFIG_LV_DEF_REFR_PERIOD);
    return disp;
}

lv_indev_t* lvgl_add_touch(lv_display_t* disp, TouchPanel& touch) {
    if (!touch.handle()) return nullptr;
    lvgl_port_touch_cfg_t tc = {};
    tc.disp = disp;
    tc.handle = touch.handle();
    tc.scale.x = 1.f;
    tc.scale.y = 1.f;
    lv_indev_t* in = lvgl_port_add_touch(&tc);
    if (!in) ESP_LOGE(TAG, "lvgl_port_add_touch sikertelen");
    return in;
}

uint32_t lvgl_take_frames() { return g_frames.exchange(0, std::memory_order_relaxed); }

}  // namespace og
