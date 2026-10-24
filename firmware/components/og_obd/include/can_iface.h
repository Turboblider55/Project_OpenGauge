#pragma once
// Hardverfüggetlen CAN-interfész.
//
// Mit csinál: azt az egyetlen felületet írja le, amelyet az OBD-logika (ISO-TP, kliens,
//             protokoll-felismerés, ECU-szimulátor) a CAN-buszról ismer.
// Hogyan:     három kis osztály: CanFrame (egy keret), ICanBus (küldés/fogadás/idő),
//             ICanPort (ICanBus + a busz beállítása: sebesség, csak hallgató mód).
// Miért:      a logika így hardver nélkül, PC-n is tesztelhető (VirtualBus), és ugyanaz a kód
//             fut az ESP32-C3-on (TwaiBus) és az ESP32-S2-n (ECU-szimulátor).
#include <cstdint>

namespace og {

// Egy klasszikus (nem FD) CAN-keret.
struct CanFrame {
    uint32_t id = 0;     // 11 vagy 29 bites azonosító
    bool ext = false;    // igaz: 29 bites (extended) azonosító
    uint8_t dlc = 0;     // adatbájtok száma (0..8)
    uint8_t data[8] = {0};
};

// A busz beállítása. A protokoll-felismerés ezzel vált sebességet, és ezzel kapcsol
// "csak hallgató" módba, amelyben az eszköz semmit sem ad a buszra.
struct CanConfig {
    uint32_t bitrate_bps = 500000;
    bool listen_only = false;
};

class ICanBus {
public:
    virtual ~ICanBus() = default;

    // Mit csinál: egy keretet ad a buszra. Igaz, ha a keret elment az időkorláton belül.
    // Hogyan:     a megvalósítás dolga (TWAI: twai_transmit).
    // Miért:      a hívó a visszatérésből tudja, hogy van-e busz, amely nyugtáz.
    virtual bool send(const CanFrame& f, uint32_t timeout_ms) = 0;

    // Mit csinál: legfeljebb timeout_ms-ig vár egy keretre. Igaz, ha érkezett.
    // Hogyan:     timeout_ms = 0 esetén csak azt nézi, van-e már várakozó keret.
    // Miért:      az időkorlát teszi lehetővé a P2/P2* időzítések és a hibák (elmaradó
    //             keret) kezelését.
    virtual bool recv(CanFrame& f, uint32_t timeout_ms) = 0;

    // Mit csinál: a busz saját időbélyege milliszekundumban (átfordulhat).
    // Hogyan:     valós hardveren esp_timer, szimulációban a virtuális óra.
    // Miért:      a határidők számolásához kell, anélkül hogy a logika a hardveres
    //             időkezelést ismerné; a szimuláció így várakozás nélkül, gyorsan fut.
    virtual uint32_t now_ms() = 0;
};

class ICanPort : public ICanBus {
public:
    // Mit csinál: sebességet és üzemmódot állít. Igaz, ha sikerült.
    // Hogyan:     TWAI-nál a driver újratelepítése az új időzítéssel és móddal.
    // Miért:      a protokoll-felismerés előbb csak hallgat (nem zavarja a járműbuszt
    //             rossz sebességgel), és csak a helyes sebesség megtalálása után ad.
    virtual bool configure(const CanConfig& cfg) = 0;
};

}  // namespace og
