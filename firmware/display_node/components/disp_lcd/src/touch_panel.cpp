#include "touch_panel.h"

#include <cstdio>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace og {

namespace {

const char* TAG = "touch";

constexpr uint8_t kRegDisAutoSleep = 0xFE;  // CST816S: nem nulla érték tiltja az automatikus alvást

#define OG_CHECK(expr)                                                  \
    do {                                                                \
        esp_err_t err_ = (expr);                                        \
        if (err_ != ESP_OK) {                                           \
            ESP_LOGE(TAG, "%s: %s", #expr, esp_err_to_name(err_));      \
            return false;                                               \
        }                                                               \
    } while (0)

}  // namespace

// Mit csinál: a 0x08..0x77 címeken megszólítja a buszt, és naplózza, ki válaszol.
// Miért:      első indításkor ebből látszik a CST816S címe (adatlap szerint 0x15), és hogy a
//             busz felhúzó-ellenállásai rendben vannak-e (ha senki sem válaszol, gyanús).
void TouchPanel::scan_bus() {
    char found[96] = "";
    size_t n = 0;
    int count = 0;
    for (uint16_t a = 0x08; a <= 0x77; ++a) {
        if (i2c_master_probe(static_cast<i2c_master_bus_handle_t>(bus_), a, 20) == ESP_OK) {
            n += std::snprintf(found + n, sizeof found - n, " 0x%02X", a);
            ++count;
        }
    }
    if (count)
        ESP_LOGI(TAG, "I2C-keresés: %d eszköz:%s", count, found);
    else
        ESP_LOGW(TAG, "I2C-keresés: egyetlen eszköz sem válaszolt (bekötés, felhúzó-ellenállás?)");
}

bool TouchPanel::init(const TouchConfig& cfg) {
    // I2C-busz a belső felhúzókkal. A belső ellenállás (~45 kOhm) 100 kHz-hez rövid
    // vezetékkel elegendő; ha a modulon nincs külső felhúzó (Q-02), ez tartja a buszt.
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = static_cast<gpio_num_t>(cfg.pins.sda);
    bc.scl_io_num = static_cast<gpio_num_t>(cfg.pins.scl);
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus = nullptr;
    OG_CHECK(i2c_new_master_bus(&bc, &bus));
    bus_ = bus;

    // Reset-impulzus a keresés előtt: így a chip biztosan ébren van.
    if (cfg.pins.rst >= 0) {
        const gpio_num_t rst = static_cast<gpio_num_t>(cfg.pins.rst);
        gpio_reset_pin(rst);
        gpio_set_direction(rst, GPIO_MODE_OUTPUT);
        gpio_set_level(rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst, 1);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    scan_bus();

    // A panel-IO a meghajtó makrójával azonos értékekkel (a makró mezősorrendje C++-ban nem fordul).
    esp_lcd_panel_io_i2c_config_t ioc = {};
    ioc.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
    ioc.scl_speed_hz = 100000;
    ioc.control_phase_bytes = 1;
    ioc.lcd_cmd_bits = 8;
    ioc.lcd_param_bits = 0;
    ioc.flags.disable_control_phase = 1;
    OG_CHECK(esp_lcd_new_panel_io_i2c_v2(bus, &ioc, &io_));

    esp_lcd_touch_config_t tc = {};
    tc.x_max = cfg.x_max;
    tc.y_max = cfg.y_max;
    tc.rst_gpio_num = static_cast<gpio_num_t>(cfg.pins.rst);
    tc.int_gpio_num = static_cast<gpio_num_t>(cfg.pins.intr);
    tc.levels.reset = 0;
    tc.levels.interrupt = 0;
    tc.flags.swap_xy = cfg.swap_xy;
    tc.flags.mirror_x = cfg.mirror_x;
    tc.flags.mirror_y = cfg.mirror_y;
    OG_CHECK(esp_lcd_touch_new_i2c_cst816s(io_, &tc, &tp_));

    const uint8_t on = 0x01;
    const esp_err_t e = esp_lcd_panel_io_tx_param(io_, kRegDisAutoSleep, &on, 1);
    ESP_LOGI(TAG, "CST816S kész (0x%02X), automatikus alvás tiltása: %s; tengelycsere %d, tükrözés x %d, y %d",
             ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS, esp_err_to_name(e), cfg.swap_xy, cfg.mirror_x, cfg.mirror_y);
    return true;
}

}  // namespace og
