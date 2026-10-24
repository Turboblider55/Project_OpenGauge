#pragma once
// A kerek 1,85"-es kijelző (ST77916, QSPI, 360 x 360) alacsony szintű meghajtása.
//
// Mit csinál: felállítja a QSPI-buszt és az ST77916 panelt, vezérli a háttérvilágítást
//             (PWM), és téglalapokat tölt ki vagy képrészleteket rajzol a panelre.
// Hogyan:     az ESP-IDF esp_lcd rétegére és az Espressif esp_lcd_st77916 komponensére épül.
//             A rajzolás egy 40 soros, DMA-képes pufferen át, sávonként történik; a puffert
//             csak az előző átvitel végét jelző callback után írja újra.
// Miért:      ez az első lépés a kijelző felé (M2): a színtesztek ezzel igazolják a bekötést,
//             a szín- és bájtsorrendet; később az LVGL ugyanezt a panel-handle-t használja.
#include <cstdint>

#include "esp_lcd_types.h"

namespace og {

// A kijelző lábai (GPIO-számok; -1 = nincs bekötve).
struct LcdPins {
    int sclk;
    int d0, d1, d2, d3;
    int cs;
    int rst;
    int te;  // Tearing Effect (függőleges szinkron); a színteszt még nem használja
    int bl;  // háttérvilágítás (PWM)
};

// Az ST77916 init-sorozata (a panel gyári regiszterbeállításai).
enum class LcdInit : uint8_t {
    Waveshare185,      // Waveshare 1,85" kerek panel (st77916_waveshare_1_85.c)
    ComponentDefault,  // az esp_lcd_st77916 komponens alapértelmezett táblája
};

struct LcdConfig {
    LcdPins pins;
    LcdInit init = LcdInit::Waveshare185;
    uint32_t pclk_hz = 20 * 1000 * 1000;
    bool swap_bytes = true;  // RGB565 bájtcsere: a panel nagy-endián sorrendet vár
    bool bgr = false;        // BGR színsorrend a panelen
};

// RGB565 szín 8 bites komponensekből.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

class LcdPanel {
public:
    static constexpr int kWidth = 360;
    static constexpr int kHeight = 360;
    static constexpr int kStripLines = 40;  // a rajzolópuffer magassága (sor)

    // Mit csinál: QSPI-busz, panel-IO, ST77916 és háttérvilágítás felállítása; a panel
    //             bekapcsolva, a háttérvilágítás kikapcsolva marad.
    // Hogyan:     spi_bus_initialize -> esp_lcd_new_panel_io_spi (QSPI) -> esp_lcd_new_panel_st77916
    //             -> reset, init, disp_on; LEDC 5 kHz, 10 bit.
    // Miért:      a háttérvilágítást a hívó kapcsolja be az első kép után, így bekapcsoláskor
    //             nem villan fel a panel véletlenszerű memóriatartalma.
    bool init(const LcdConfig& cfg);

    // Háttérvilágítás 0..100 %.
    void set_backlight(uint8_t percent);

    // Mit csinál: egyszínű téglalapot rajzol (a szín natív RGB565, a cserét ez végzi).
    // Hogyan:     a puffert legfeljebb kStripLines sornyi színnel tölti, és sávonként küldi.
    bool fill_rect(int x, int y, int w, int h, uint16_t color);

    // Mit csinál: pixeltömböt rajzol (natív RGB565, sorfolytonosan, w*h elem).
    // Hogyan:     sávonként átmásolja a DMA-pufferbe (közben bájtcserével), majd elküldi.
    // Miért:      a forrás lehet flash vagy PSRAM is, nem kell DMA-képesnek lennie.
    bool draw_pixels(int x, int y, int w, int h, const uint16_t* px);

    esp_lcd_panel_handle_t panel() const { return panel_; }
    esp_lcd_panel_io_handle_t io() const { return io_; }

private:
    bool send_strip(int x, int y, int w, int h);
    uint16_t to_panel(uint16_t c) const { return swap_ ? static_cast<uint16_t>((c >> 8) | (c << 8)) : c; }

    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint16_t* buf_ = nullptr;
    void* done_ = nullptr;  // SemaphoreHandle_t (a FreeRTOS-fejléc ne kerüljön a publikus fejlécbe)
    bool swap_ = true;
    int bl_gpio_ = -1;
};

}  // namespace og
