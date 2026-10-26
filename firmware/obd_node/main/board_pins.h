#pragma once
// Az OBD-egység lábkiosztása, hardverváltozatonként.
//
// Mit csinál: megadja a CAN-adó-vevő bekötését (TX, RX és az üzemmódláb, ha van).
// Hogyan:     a változatot a menuconfig választja (OpenGauge: OBD-egység -> Hardverváltozat).
// Miért:      a lábak csak itt szerepelnek; az asztali teszt és a saját NYÁK között csak ez
//             a fájl tér el, a program többi része változatlan.
#include "sdkconfig.h"

namespace og::board {

#if defined(CONFIG_OG_BOARD_DEVKIT_C3)
// Asztali teszt: ESP32-C3 SuperMini, SN65HVD230 CAN-TTL modul.
constexpr int kPinCanTx = 4;
constexpr int kPinCanRx = 3;
constexpr int kPinCanRs = -1;  // a modulon az RS láb nincs kivezetve (nagy sebességű mód)
#elif defined(CONFIG_OG_BOARD_OBD_REV_A)
// Saját NYÁK, Rev. A (hardver_architektura 2.3.: IO4 TX, IO5 RX, IO10 RS).
constexpr int kPinCanTx = 4;
constexpr int kPinCanRx = 5;
constexpr int kPinCanRs = 10;  // SN65HVD231 RS: LOW = nagy sebesség, HIGH = készenlét
#else
#error "Nincs kiválasztva hardverváltozat (menuconfig: OpenGauge: OBD-egység)"
#endif

}  // namespace og::board
