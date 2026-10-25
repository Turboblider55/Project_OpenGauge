#pragma once
// ESP-NOW vételi teszt, kijelző nélkül (részletek: espnow_rx_test.cpp).

namespace og {

// Mit csinál: fogadja a telemetriát és másodpercenként naplózza a vételi statisztikát
//             (nem tér vissza, csak ha a rádió nem indul el).
void run_espnow_rx_test();

}  // namespace og
