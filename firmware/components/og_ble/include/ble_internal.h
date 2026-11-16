#pragma once
// Az og_ble belső összekötője: a veremindítót a ble_radio.cpp adja, az AMS-kliens (ble_ams.cpp) használja.
#include <cstdint>

extern "C" void ble_store_config_init(void);

namespace og {

// Mit csinál: elindítja a NimBLE-t a megadott eszköznévvel; a custom_service a teszt-számláló
//             szolgáltatást veszi fel, a secure a párosítást és a kötést (bonding) kapcsolja be.
// Hogyan:     a GATT-táblát a verem indulása előtt kell felvenni, ezért a beállítások az indításkor érvényesek.
bool ble_internal_start(const char* name, bool custom_service, bool secure);

// Mit csinál: igaz, ha a NimBLE fut (a ble_stop() hívásától hamis); a háttérfeladatok ebből tudják, hogy nem hívhatnak verem-függvényt.
bool ble_internal_running();

// Mit csinál: a host szinkronja után a saját címtípus (a hirdetéshez és a kapcsolódáshoz).
uint8_t ble_internal_addr_type();

}  // namespace og
