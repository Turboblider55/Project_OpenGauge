// Kijelzőegység – belépési pont.
//
// Mit csinál: inicializálja a kerek kijelzőt (ST77916, QSPI), bekapcsolja a háttérvilágítást, és a
//             menuconfigban választott módot futtatja: műszerfal (valós telemetria, érintéssel),
//             érintésteszt, színteszt (M2), LVGL-teszt (M3) vagy ESP-NOW vételi teszt (M4,
//             kijelző nélkül).
// Hogyan:     a lábkiosztás a board_pins.h-ból, a kijelzőbeállítások a menuconfigból jönnek.
// Miért:      a kijelzős tesztek ugyanazt a panelinicializálást használják; a színteszt új hardveren
//             (pl. a saját NYÁK-on) az első lépés marad, az LVGL-teszt a felület alapja.
#include "board_pins.h"
#include "color_test.h"
#include "dash_app.h"
#include "esp_log.h"
#include "espnow_rx_test.h"
#include "lcd_panel.h"
#include "lvgl_test.h"
#include "touch_test.h"
#include "og_codec.h"
#include "sdkconfig.h"

namespace {
const char* TAG = "og_disp";
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "OpenGauge kijelzőegység indul; protokoll v%d", og::kProtoVersion);
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
    if (!touch_ok) ESP_LOGW(TAG, "Az érintővezérlő nem érhető el");
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
    og::run_dash_app(lcd, cfg.swap_bytes, touch_ok ? &touch : nullptr);
#elif defined(CONFIG_OG_DISP_TEST_TOUCH)
    og::run_touch_test(lcd, cfg.swap_bytes, touch);
#elif defined(CONFIG_OG_DISP_TEST_COLORS)
    og::run_color_test(lcd);
#else
    og::run_lvgl_test(lcd, cfg.swap_bytes);
#endif
}
