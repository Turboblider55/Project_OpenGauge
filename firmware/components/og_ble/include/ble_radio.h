#pragma once
// BLE-hirdetés és -szkennelés a két egység közti ébresztéshez (ESP-IDF NimBLE illesztő).
//
// Mit csinál: az OBD-egység nem csatlakoztatható (non-connectable) hirdetést ad rövid, saját
//             adatcsomaggal; a kijelzőegység passzívan szkennel, és számolja a vett hirdetéseket.
// Hogyan:     a hirdetés gyártói adata: cégazonosító 0xFFFF (teszt), "OG", szerepkör, másodperc-
//             számláló. A számláló másodpercenként nő, így a szkenner a kimaradt másodperceket is
//             látja. A hirdetési és szkennelési időzítés a hívó dolga (a tesztben folyamatos).
// Miért:      a BLE-ébresztés (S-13) alapfeltétele, hogy a C3 hirdetését az S3 megbízhatóan, kis
//             késéssel észrevegye; ezt ESP-NOW nélkül, majd azzal együtt kell mérni (H-16).
#include <cstdint>

namespace og {

enum class BleRole : uint8_t { ObdNode = 1, DisplayNode = 2 };

struct BleScanStats {
    uint32_t adverts = 0;      // a vett OG-hirdetések száma (összes)
    uint32_t seconds_seen = 0; // hány különböző másodperc-számláló értéket láttunk
    uint32_t seconds_lost = 0; // a számláló ugrásai alapján kimaradt másodpercek
    int rssi = 0;              // az utolsó hirdetés térereje (dBm)
    int32_t last_counter = -1; // az utolsó vett számláló (-1: még nem jött)
};

// Mit csinál: elindítja a NimBLE-t, és a megadott szerepkörrel folyamatosan hirdet
//             (itvl_ms: hirdetési periódus). Hamis, ha a verem nem indult el.
bool ble_start_advertising(BleRole role, uint32_t itvl_ms);

// Mit csinál: elindítja a NimBLE-t és passzív szkennelést kezd (window_pct: a szkennelési
//             ablak aránya a periódusból, 100 = folyamatos). Hamis, ha a verem nem indult el.
bool ble_start_scanning(uint32_t window_pct);

// Mit csinál: pillanatfelvétel a szkenner statisztikájáról.
BleScanStats ble_scan_stats();

}  // namespace og
