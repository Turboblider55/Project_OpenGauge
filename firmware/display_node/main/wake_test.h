#pragma once
// Alvás- és ébredési teszt a kijelzőegységen, kijelző nélkül (részletek: wake_test.cpp).
#include "status_led.h"

namespace og {

// Mit csinál: ESP-NOW életjelet küld és figyel; az OBD-egység ALVÁS üzenetére light sleepbe lép, és
//             időzített BLE-szkenneléssel várja az ébresztő hirdetést (nem tér vissza).
void run_wake_test(StatusLed& led);

}  // namespace og
