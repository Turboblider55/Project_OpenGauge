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

struct BleConnStats {
    bool connected = false;        // van-e élő kapcsolat
    bool subscribed = false;       // feliratkozott-e a kliens az értesítésre
    uint32_t connects = 0;         // sikeres kapcsolódások
    uint32_t disconnects = 0;      // bontások
    int last_reason = 0;           // az utolsó bontás oka (NimBLE-kód; 0x08 = időtúllépés, 0x13/0x16 = a másik fél bontott)
    uint32_t itvl_us = 0;          // a kapcsolat jelenlegi periódusa
    uint32_t notify_ok = 0;        // elküldött értesítések
    uint32_t notify_fail = 0;      // sikertelen értesítések
    uint32_t counter = 0;          // a jellemző jelenlegi értéke
};

// Mit csinál: elindítja a NimBLE-t, és csatlakoztatható hirdetést ad a megadott néven; egy
//             GATT-szolgáltatást nyit egyetlen olvasható és értesíthető jellemzővel
//             (másodpercenként növekvő számláló). Bontás után a hirdetést újraindítja.
// Miért:      a telefonos kapcsolat (média-vezérlés, S-14) stabilitása ESP-NOW mellett a H-16
//             szerint kockázatos; ez a legegyszerűbb terhelés, amin mérni lehet.
// Fontos:     a ble_start_scanning előtt kell hívni (a GATT-táblát a verem indulása előtt kell felvenni).
bool ble_start_peripheral(const char* name);

// Mit csinál: pillanatfelvétel a kapcsolat statisztikájáról.
BleConnStats ble_conn_stats();

// Mit csinál: elindítja a NimBLE-t, és a megadott szerepkörrel folyamatosan hirdet
//             (itvl_ms: hirdetési periódus). Hamis, ha a verem nem indult el.
bool ble_start_advertising(BleRole role, uint32_t itvl_ms);

// Mit csinál: elindítja a NimBLE-t és passzív szkennelést kezd (window_pct: a szkennelési
//             ablak aránya a periódusból, 100 = folyamatos). Hamis, ha a verem nem indult el.
bool ble_start_scanning(uint32_t window_pct);

// Mit csinál: pillanatfelvétel a szkenner statisztikájáról.
BleScanStats ble_scan_stats();

}  // namespace og
