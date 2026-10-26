#include "lcd_panel.h"

#include <cstring>

#include "driver/ledc.h"
#include "esp_attr.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "st77916_waveshare_1_85.h"

namespace og {

namespace {

const char* TAG = "lcd";

constexpr spi_host_device_t kHost = SPI2_HOST;
constexpr ledc_mode_t kBlMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kBlTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kBlChannel = LEDC_CHANNEL_0;
constexpr uint32_t kBlMaxDuty = (1u << 10) - 1;  // 10 bites felbontás

// Mit csinál: jelzi, hogy a pufferből indított színadat-átvitel befejeződött.
// Hogyan:     megszakításból hívódik; a szemafort adja, a rajzoló feladat ezen vár.
// Miért:      a DMA a puffert közvetlenül olvassa, ezért csak ezután szabad újraírni.
bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &woken);
    return woken == pdTRUE;
}

#define OG_CHECK(expr)                                                            \
    do {                                                                          \
        esp_err_t err_ = (expr);                                                  \
        if (err_ != ESP_OK) {                                                     \
            ESP_LOGE(TAG, "%s: %s", #expr, esp_err_to_name(err_));                \
            return false;                                                         \
        }                                                                         \
    } while (0)

}  // namespace

bool LcdPanel::init(const LcdConfig& cfg) {
    swap_ = cfg.swap_bytes;
    bl_gpio_ = cfg.pins.bl;

    done_ = xSemaphoreCreateBinary();
    buf_ = static_cast<uint16_t*>(
        heap_caps_malloc(kWidth * kStripLines * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!done_ || !buf_) {
        ESP_LOGE(TAG, "Nincs elég belső (DMA-képes) memória a rajzolópufferhez");
        return false;
    }

    // Háttérvilágítás: PWM, kezdetben kikapcsolva.
    if (bl_gpio_ >= 0) {
        ledc_timer_config_t t = {};
        t.speed_mode = kBlMode;
        t.duty_resolution = LEDC_TIMER_10_BIT;
        t.timer_num = kBlTimer;
        t.freq_hz = 5000;
        t.clk_cfg = LEDC_AUTO_CLK;
        OG_CHECK(ledc_timer_config(&t));
        ledc_channel_config_t c = {};
        c.gpio_num = bl_gpio_;
        c.speed_mode = kBlMode;
        c.channel = kBlChannel;
        c.timer_sel = kBlTimer;
        c.duty = 0;
        OG_CHECK(ledc_channel_config(&c));
    }

    // QSPI-busz. Az ST77916_PANEL_BUS_QSPI_CONFIG makró mezősorrendje C++-ban nem fordul,
    // ezért kézzel, ugyanazokkal az értékekkel töltjük ki.
    spi_bus_config_t bus = {};
    bus.sclk_io_num = cfg.pins.sclk;
    bus.data0_io_num = cfg.pins.d0;
    bus.data1_io_num = cfg.pins.d1;
    bus.data2_io_num = cfg.pins.d2;
    bus.data3_io_num = cfg.pins.d3;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = kWidth * kStripLines * sizeof(uint16_t);
    OG_CHECK(spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO));

    // Panel-IO: QSPI, 32 bites parancs (a QSPI-keret fejléce), 8 bites paraméterek.
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = cfg.pins.cs;
    io.dc_gpio_num = -1;
    io.spi_mode = 0;
    io.pclk_hz = cfg.pclk_hz;
    io.trans_queue_depth = 10;
    io.on_color_trans_done = on_color_done;
    io.user_ctx = done_;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    OG_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kHost), &io, &io_));

    // ST77916 a panelhez tartozó gyári init-sorozattal (lásd LcdInit).
    st77916_vendor_config_t vendor = {};
    vendor.flags.use_qspi_interface = 1;
    if (cfg.init == LcdInit::Waveshare185) {
        vendor.init_cmds = og_st77916_waveshare_1_85_init;
        vendor.init_cmds_size = og_st77916_waveshare_1_85_init_size;
    }
    esp_lcd_panel_dev_config_t dev = {};
    dev.reset_gpio_num = cfg.pins.rst;
    dev.rgb_ele_order = cfg.bgr ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB;
    dev.bits_per_pixel = 16;
    dev.vendor_config = &vendor;
    OG_CHECK(esp_lcd_new_panel_st77916(io_, &dev, &panel_));
    OG_CHECK(esp_lcd_panel_reset(panel_));
    OG_CHECK(esp_lcd_panel_init(panel_));
    OG_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "ST77916 kész: %dx%d, QSPI %lu MHz, bájtcsere %s, %s, init: %s", kWidth, kHeight,
             (unsigned long)(cfg.pclk_hz / 1000000), swap_ ? "be" : "ki", cfg.bgr ? "BGR" : "RGB",
             cfg.init == LcdInit::Waveshare185 ? "Waveshare 1,85\"" : "komponens alapértelmezett");
    return true;
}

void LcdPanel::set_backlight(uint8_t percent) {
    if (bl_gpio_ < 0) return;
    if (percent > 100) percent = 100;
    ledc_set_duty(kBlMode, kBlChannel, kBlMaxDuty * percent / 100);
    ledc_update_duty(kBlMode, kBlChannel);
}

// Mit csinál: a puffer első w*h pixelét a panel (x, y) pontjára küldi, és megvárja a végét.
// Hogyan:     esp_lcd_panel_draw_bitmap (a vég koordináta kizárólagos), majd a szemafor.
bool LcdPanel::send_strip(int x, int y, int w, int h) {
    if (esp_lcd_panel_draw_bitmap(panel_, x, y, x + w, y + h, buf_) != ESP_OK) return false;
    return xSemaphoreTake(static_cast<SemaphoreHandle_t>(done_), pdMS_TO_TICKS(1000)) == pdTRUE;
}

bool LcdPanel::fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!panel_ || w <= 0 || h <= 0) return false;
    const uint16_t c = to_panel(color);
    const int lines = h < kStripLines ? h : kStripLines;
    for (int i = 0; i < w * lines; ++i) buf_[i] = c;
    for (int row = 0; row < h; row += kStripLines) {
        const int n = (h - row) < kStripLines ? (h - row) : kStripLines;
        if (!send_strip(x, y + row, w, n)) return false;
    }
    return true;
}

bool LcdPanel::draw_pixels(int x, int y, int w, int h, const uint16_t* px) {
    if (!panel_ || w <= 0 || h <= 0 || w > kWidth) return false;
    for (int row = 0; row < h; row += kStripLines) {
        const int n = (h - row) < kStripLines ? (h - row) : kStripLines;
        const uint16_t* src = px + row * w;
        for (int i = 0; i < w * n; ++i) buf_[i] = to_panel(src[i]);
        if (!send_strip(x, y + row, w, n)) return false;
    }
    return true;
}

}  // namespace og
