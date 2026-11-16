#pragma once
// Az OBD-egység lábkiosztása, hardverváltozatonként.
//
// Mit csinál: megadja a CAN-adó-vevő bekötését (TX, RX és az üzemmódláb, ha van) és a LED-eket.
// Hogyan:     a változatot a menuconfig választja (OpenGauge: OBD-egység -> Hardverváltozat).
// Miért:      a lábak csak itt szerepelnek; az asztali teszt és a saját NYÁK között csak ez
//             a fájl tér el, a program többi része változatlan.
#include "sdkconfig.h"
#include "status_led.h"

namespace og::board {

#if defined(CONFIG_OG_BOARD_DEVKIT_C3)
// Asztali teszt: ESP32-C3 SuperMini, SN65HVD230 CAN-TTL modul.
constexpr int kPinCanTx = 4;
constexpr int kPinCanRx = 3;
constexpr int kPinCanRs = -1;  // a modulon az RS láb nincs kivezetve (nagy sebességű mód)
constexpr LedPins kLeds = {-1, -1, -1, 0, 0, 0};  // a SuperMini LED-je nem állapotjelzésre való
#elif defined(CONFIG_OG_BOARD_OBD_REV_A)
// Saját NYÁK, Rev. A (hardver_architektura 2.3.: IO4 TX, IO5 RX, IO10 RS).
constexpr int kPinCanTx = 4;
constexpr int kPinCanRx = 5;
constexpr int kPinCanRs = 10;  // SN65HVD231 RS: LOW = nagy sebesség, HIGH = készenlét
// Piros IO8, zöld IO6, kék IO7, 330 Ω-on át a földre (aktív magas). A százalék a színek eltérő
// fényerejét egyenlíti ki.
constexpr LedPins kLeds = {.red = 8, .green = 6, .blue = 7, .red_pct = 25, .green_pct = 40, .blue_pct = 40};
#else
#error "Nincs kiválasztva hardverváltozat (menuconfig: OpenGauge: OBD-egység)"
#endif

}  // namespace og::board
