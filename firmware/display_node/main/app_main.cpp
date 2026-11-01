// Kijelzőegység – belépési pont.
//
// Mit csinál: inicializálja a kerek kijelzőt (ST77916, QSPI), bekapcsolja a háttérvilágítást, és a
//             menuconfigban választott módot futtatja: műszerfal (valós telemetria, érintéssel),
//             érintésteszt, színteszt (M2), LVGL-teszt (M3) vagy ESP-NOW vételi teszt (M4,
//             kijelző nélkül). Elsőként a LED-eket indítja, hogy az indítási hibák is látsszanak.
// Hogyan:     a lábkiosztás a board_pins.h-ból, a kijelzőbeállítások a menuconfigból jönnek.
// Miért:      a kijelzős tesztek ugyanazt a panelinicializálást használják; a színteszt új hardveren
//             (pl. a saját NYÁK-on) az első lépés marad, az LVGL-teszt a felület alapja.
#include "ble_radio.h"
#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "color_test.h"
#include "dash_app.h"
#include "esp_log.h"
#include "espnow_rx_test.h"
#include "lcd_panel.h"
#include "led_pattern.h"
#include "lvgl_test.h"
#include "touch_test.h"
#include "og_codec.h"
#include "sdkconfig.h"
#include "status_led.h"

namespace {
const char* TAG = "og_disp";
og::StatusLed g_led;
}

#if defined(CONFIG_OG_DISP_TEST_BLE) || defined(CONFIG_OG_DISP_TEST_COEX)
// Mit csinál: másodpercenként naplózza a BLE szkennelés és (ha van) a kapcsolat statisztikáját.
// Miért:      a koegzisztencia-teszt mellett az ESP-NOW vételi teszt is naplóz, a két sor együtt mutatja,
//             hogy a BLE terhelése mikor rontja a rádióvételt (H-16).
void ble_report_task(void*) {
    uint32_t prev = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const og::BleScanStats s = og::ble_scan_stats();
        ESP_LOGI(TAG, "BLE szkenner: %lu hirdetés/s, számláló %ld, látott mp %lu, kimaradt mp %lu, RSSI %d dBm",
                 static_cast<unsigned long>(s.adverts - prev), static_cast<long>(s.last_counter),
                 static_cast<unsigned long>(s.seconds_seen), static_cast<unsigned long>(s.seconds_lost), s.rssi);
        prev = s.adverts;
#if defined(CONFIG_OG_DISP_TEST_COEX)
        const og::BleConnStats c = og::ble_conn_stats();
        ESP_LOGI(TAG, "BLE kapcsolat: %s, értesítés %s, periódus %lu us, kapcsolódás %lu, bontás %lu (ok %d), értesítés ok/hiba %lu/%lu",
                 c.connected ? "VAN" : "nincs", c.subscribed ? "igen" : "nem", static_cast<unsigned long>(c.itvl_us),
                 static_cast<unsigned long>(c.connects), static_cast<unsigned long>(c.disconnects), c.last_reason,
                 static_cast<unsigned long>(c.notify_ok), static_cast<unsigned long>(c.notify_fail));
#endif
    }
}
#endif

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "OpenGauge kijelzőegység indul; protokoll v%d", og::kProtoVersion);
    if (!g_led.start(og::board::kLeds)) ESP_LOGE(TAG, "A LED-ek nem indultak el");
#if defined(CONFIG_OG_DISP_TEST_BLE)
    // BLE-szkennelési teszt: a kijelző nem indul; másodpercenként a vételi statisztika kerül a naplóba.
    if (!og::ble_start_scanning(CONFIG_OG_BLE_SCAN_WINDOW_PCT)) g_led.set_fault(og::Fault::Init, true);
    ble_report_task(nullptr);
#endif
#if defined(CONFIG_OG_DISP_TEST_COEX)
    // Koegzisztencia: a BLE (periféria + szkenner) a 0. magon fut, mellette az ESP-NOW vételi teszt.
    // A periféria indul előbb (a GATT-táblát a verem indulása előtt kell felvenni).
    if (!og::ble_start_peripheral("OpenGauge") || !og::ble_start_scanning(CONFIG_OG_BLE_SCAN_WINDOW_PCT)) {
        g_led.set_fault(og::Fault::Init, true);
    }
    xTaskCreate(ble_report_task, "ble_report", 4096, nullptr, 2, nullptr);
    og::run_espnow_rx_test();
    return;
#endif
#if defined(CONFIG_OG_DISP_TEST_ESPNOW)
    // A kijelzőt nem inicializáljuk: a panel nem kap képet, a vételi teszt csak naplóz.
    og::run_espnow_rx_test();
    return;
#endif

#if defined(CONFIG_OG_DISP_TEST_DASH) || defined(CONFIG_OG_DISP_TEST_TOUCH)
    // Érintő: reset, majd azonnal az automatikus alvás tiltása (a chip a reset után néhány száz
    // ms-mal elalszik). A tájolás a menuconfigból igazítható a kijelzőhöz.
    og::TouchConfig tc;
    tc.pins = og::board::kTouch;
#ifdef CONFIG_OG_TOUCH_SWAP_XY
    tc.swap_xy = true;
#endif
#ifdef CONFIG_OG_TOUCH_MIRROR_X
    tc.mirror_x = true;
#endif
#ifdef CONFIG_OG_TOUCH_MIRROR_Y
    tc.mirror_y = true;
#endif
#ifdef CONFIG_OG_TOUCH_USE_INT
    tc.use_interrupt = true;
#endif
    static og::TouchPanel touch;
    const bool touch_ok = touch.init(tc);
    if (!touch_ok) {
        ESP_LOGW(TAG, "Az érintővezérlő nem érhető el");
        g_led.set_fault(og::Fault::Init, true);
    }
#endif


    og::LcdConfig cfg;
    cfg.pins = og::board::kLcd;
    cfg.pclk_hz = CONFIG_OG_LCD_PCLK_MHZ * 1000 * 1000;
#ifdef CONFIG_OG_LCD_SWAP_BYTES
    cfg.swap_bytes = true;
#else
    cfg.swap_bytes = false;
#endif
#ifdef CONFIG_OG_LCD_BGR
    cfg.bgr = true;
#endif
#ifdef CONFIG_OG_LCD_INIT_COMPONENT_DEFAULT
    cfg.init = og::LcdInit::ComponentDefault;
#endif

    static og::LcdPanel lcd;
    if (!lcd.init(cfg)) {
        ESP_LOGE(TAG, "A kijelző nem indult el; a bekötést és a tápot kell ellenőrizni");
        g_led.set_fault(og::Fault::Init, true);
        return;
    }
    lcd.fill_rect(0, 0, og::LcdPanel::kWidth, og::LcdPanel::kHeight, 0);  // induláskor véletlenszerű a panel memóriája
    lcd.set_backlight(CONFIG_OG_LCD_BACKLIGHT_PERCENT);
    ESP_LOGI(TAG, "Háttérvilágítás: %d %%", CONFIG_OG_LCD_BACKLIGHT_PERCENT);
#if defined(CONFIG_OG_DISP_TEST_DASH) || defined(CONFIG_OG_DISP_TEST_TOUCH)
    // Ellenőrzés: a kijelző indítása után is ébren van-e az érintő (a reset-lábak felcserélése
    // éppen így derült ki: rossz lábkiosztásnál a kijelző resetje az érintőt indította újra).
    ESP_LOGI(TAG, "Érintő a kijelző indítása után: %s", touch.responds() ? "válaszol" : "NEM válaszol");
#endif

#if defined(CONFIG_OG_DISP_TEST_DASH)
    og::run_dash_app(lcd, cfg.swap_bytes, touch_ok ? &touch : nullptr, g_led);
#elif defined(CONFIG_OG_DISP_TEST_TOUCH)
    og::run_touch_test(lcd, cfg.swap_bytes, touch);
#elif defined(CONFIG_OG_DISP_TEST_COLORS)
    og::run_color_test(lcd);
#else
    og::run_lvgl_test(lcd, cfg.swap_bytes);
#endif
}
