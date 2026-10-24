// Kijelzőegység – belépési pont.
//
// Mit csinál: inicializálja a kerek kijelzőt (ST77916, QSPI), bekapcsolja a háttérvilágítást, és a
//             menuconfigban választott tesztet futtatja: színteszt (M2) vagy LVGL-teszt (M3).
// Hogyan:     a lábkiosztás a board_pins.h-ból, a kijelzőbeállítások a menuconfigból jönnek.
// Miért:      a két teszt ugyanazt a panelinicializálást használja; a színteszt új hardveren
//             (pl. a saját NYÁK-on) az első lépés marad, az LVGL-teszt a felület alapja.
#include "board_pins.h"
#include "color_test.h"
#include "esp_log.h"
#include "lcd_panel.h"
#include "lvgl_test.h"
#include "og_codec.h"
#include "sdkconfig.h"

namespace {
const char* TAG = "og_disp";
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "OpenGauge kijelzőegység indul; protokoll v%d", og::kProtoVersion);

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

#if defined(CONFIG_OG_DISP_TEST_COLORS)
    og::run_color_test(lcd);
#else
    og::run_lvgl_test(lcd, cfg.swap_bytes);
#endif
}
