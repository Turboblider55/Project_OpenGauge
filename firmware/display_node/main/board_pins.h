#pragma once
// A kijelzőegység lábkiosztása, hardverváltozatonként.
//
// Mit csinál: megadja a kijelző (QSPI), a háttérvilágítás és az érintővezérlő (I2C) bekötését.
// Hogyan:     a változatot a menuconfig választja (OpenGauge: kijelzőegység -> Hardverváltozat).
// Miért:      a lábak csak itt szerepelnek; az asztali teszt és a saját NYÁK között csak ez a
//             fájl tér el, a meghajtó (disp_lcd) és a felület kódja változatlan.
#include "lcd_panel.h"
#include "touch_panel.h"
#include "sdkconfig.h"

namespace og::board {

#if defined(CONFIG_OG_BOARD_DEVKIT_S3)
// Asztali teszt: Waveshare ESP32-S3-DEV-KIT-N16R8, a modul 15 pólusú GH1.25 csatlakozóján át
// (a Waveshare bekötési ábrája szerint).
constexpr LcdPins kLcd = {
    .sclk = 4, .d0 = 9, .d1 = 8, .d2 = 7, .d3 = 6, .cs = 3, .rst = 39, .te = 18, .bl = 5};
constexpr TouchPins kTouch = {.sda = 10, .scl = 11, .rst = 40, .intr = 41};
#elif defined(CONFIG_OG_BOARD_DISP_REV_A)
// Saját NYÁK, Rev. A (hardver_architektura 3.3., J2 FPC).
constexpr LcdPins kLcd = {
    .sclk = 41, .d0 = 40, .d1 = 39, .d2 = 38, .d3 = 21, .cs = 48, .rst = 45, .te = 47, .bl = 42};
constexpr TouchPins kTouch = {.sda = 12, .scl = 13, .rst = 14, .intr = 11};
#else
#error "Nincs kiválasztva hardverváltozat (menuconfig: OpenGauge: kijelzőegység)"
#endif

}  // namespace og::board
