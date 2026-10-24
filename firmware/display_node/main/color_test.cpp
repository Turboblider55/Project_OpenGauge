// Kijelzőegység – színteszt (M2): a panel, a QSPI-átvitel és a színbeállítások ellenőrzése.
//
// Mit csinál: körbe-körbe tesztképeket rajzol: állókép-próba, egyszínű képernyők, színsávok,
//             tájolásjelzők, színátmenetek. Minden kép előtt naplózza, mit kell látni, és méri a
//             rajzolás idejét.
// Hogyan:     a disp_lcd komponens (LcdPanel) téglalap-kitöltéssel és pixeltömbbel rajzol.
// Miért:      az LVGL nélkül, közvetlenül igazolja a bekötést, a szín- és bájtsorrendet és a
//             tájolást; új hardveren (pl. a saját NYÁK-on) ezzel érdemes kezdeni.
#include "color_test.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_panel.h"

namespace {

const char* TAG = "og_colors";

constexpr int W = og::LcdPanel::kWidth;
constexpr int H = og::LcdPanel::kHeight;
constexpr int kStrip = og::LcdPanel::kStripLines;

constexpr uint16_t kRed = og::rgb565(255, 0, 0);
constexpr uint16_t kGreen = og::rgb565(0, 255, 0);
constexpr uint16_t kBlue = og::rgb565(0, 0, 255);
constexpr uint16_t kWhite = og::rgb565(255, 255, 255);
constexpr uint16_t kBlack = og::rgb565(0, 0, 0);
constexpr uint16_t kYellow = og::rgb565(255, 255, 0);
constexpr uint16_t kCyan = og::rgb565(0, 255, 255);
constexpr uint16_t kMagenta = og::rgb565(255, 0, 255);

// Egy 40 soros sáv a színátmenethez (statikus: ne a feladat veremén legyen).
uint16_t g_strip[W * kStrip];

// Mit csinál: a teljes képernyőt egy színnel tölti, és naplózza az időt.
// Miért:      a teljes képkocka ideje a QSPI-átvitel sebességét méri (40 MHz-en ~13 ms várható).
void solid(og::LcdPanel& lcd, uint16_t c, const char* name) {
    const int64_t t0 = esp_timer_get_time();
    const bool ok = lcd.fill_rect(0, 0, W, H, c);
    const int64_t us = esp_timer_get_time() - t0;
    ESP_LOGI(TAG, "%-8s teljes kép %s: %.1f ms (%.0f kép/s)", name, ok ? "ok" : "HIBA", us / 1000.0,
             us > 0 ? 1e6 / us : 0.0);
}

// Mit csinál: 8 függőleges színsáv balról jobbra: fehér, sárga, cián, zöld, bíbor, piros, kék, fekete.
// Miért:      a sorrendből látszik a vízszintes tükrözés, a színekből a színsorrend (RGB/BGR).
void color_bars(og::LcdPanel& lcd) {
    const uint16_t bars[8] = {kWhite, kYellow, kCyan, kGreen, kMagenta, kRed, kBlue, kBlack};
    for (int i = 0; i < 8; ++i) lcd.fill_rect(i * (W / 8), 0, W / 8, H, bars[i]);
    ESP_LOGI(TAG, "Színsávok balról jobbra: fehér, sárga, cián, zöld, bíbor, piros, kék, fekete");
}

// Mit csinál: fekete háttéren piros négyzet fent középen, zöld balra középen, kék középen,
//             és egy fehér vízszintes és függőleges vonal a középpontban.
// Miért:      a kerek panelen a sarkok nem látszanak, ezért a széleken középre tett jelek
//             mutatják a tájolást (fent/lent, bal/jobb) és az esetleges eltolást.
void orientation(og::LcdPanel& lcd) {
    lcd.fill_rect(0, 0, W, H, kBlack);
    lcd.fill_rect(W / 2 - 1, 0, 2, H, kWhite);
    lcd.fill_rect(0, H / 2 - 1, W, 2, kWhite);
    lcd.fill_rect(W / 2 - 20, 12, 40, 40, kRed);
    lcd.fill_rect(12, H / 2 - 20, 40, 40, kGreen);
    lcd.fill_rect(W / 2 - 20, H / 2 - 20, 40, 40, kBlue);
    ESP_LOGI(TAG, "Tájolás: piros fent, zöld balra, kék középen, fehér kereszt a közepén");
}

// Mit csinál: három vízszintes sáv (piros, zöld, kék), mindegyikben balról jobbra
//             feketéből teljes színbe futó átmenet.
// Miért:      bájtcsere-hibánál az átmenet nem egyenletes, hanem csíkos/zajos; így ez
//             érzékenyebb próba, mint az egyszínű kép.
void gradients(og::LcdPanel& lcd) {
    for (int band = 0; band < 3; ++band) {
        for (int x = 0; x < W; ++x) {
            const uint8_t v = static_cast<uint8_t>(x * 255 / (W - 1));
            const uint16_t c = band == 0 ? og::rgb565(v, 0, 0) : band == 1 ? og::rgb565(0, v, 0) : og::rgb565(0, 0, v);
            for (int y = 0; y < kStrip; ++y) g_strip[y * W + x] = c;
        }
        for (int y = band * (H / 3); y < (band + 1) * (H / 3); y += kStrip) lcd.draw_pixels(0, y, W, kStrip, g_strip);
    }
    ESP_LOGI(TAG, "Átmenetek fentről lefelé: piros, zöld, kék; balról jobbra sötétből világosba, csíkozás nélkül");
}

}  // namespace

namespace og {

void run_color_test(LcdPanel& lcd) {
    for (;;) {
        // Állókép-próba: ugyanazt a képet sokszor újrarajzolja. Hibátlan átvitelnél a kép közben
        // mozdulatlan; ha villódzik vagy csíkok ugrálnak rajta, az átvitel közben sérülnek a bitek.
        ESP_LOGI(TAG, "Állókép-próba: piros 40-szer újrarajzolva, a képnek mozdulatlannak kell lennie");
        for (int i = 0; i < 40; ++i) lcd.fill_rect(0, 0, W, H, kRed);
        vTaskDelay(pdMS_TO_TICKS(1000));
        solid(lcd, kRed, "PIROS");
        vTaskDelay(pdMS_TO_TICKS(1500));
        solid(lcd, kGreen, "ZÖLD");
        vTaskDelay(pdMS_TO_TICKS(1500));
        solid(lcd, kBlue, "KÉK");
        vTaskDelay(pdMS_TO_TICKS(1500));
        solid(lcd, kWhite, "FEHÉR");
        vTaskDelay(pdMS_TO_TICKS(1500));
        solid(lcd, kBlack, "FEKETE");
        vTaskDelay(pdMS_TO_TICKS(1500));
        color_bars(lcd);
        vTaskDelay(pdMS_TO_TICKS(3000));
        orientation(lcd);
        vTaskDelay(pdMS_TO_TICKS(3000));
        gradients(lcd);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

}  // namespace og
