#pragma once
// Az ICanPort megvalósítása az ESP32 belső TWAI (CAN 2.0B) perifériájára.
//
// Mit csinál: a hardverfüggetlen OBD-logika (ISO-TP, kliens, protokoll-felismerés,
//             ECU-modell) ezen keresztül éri el a valódi CAN-buszt.
// Hogyan:     az ESP-IDF TWAI-driverét (driver/twai.h) használja: a configure() a drivert
//             az új sebességgel és üzemmóddal újratelepíti, a send() megvárja a keret
//             nyugtázását (TX_SUCCESS riasztás), a recv() a driver fogadási sorából olvas.
//             Igény szerint minden keretet naplóz (azonosító, DLC, adatbájtok).
// Miért:      ez az egyetlen hardverfüggő réteg a CAN-oldalon; a fölötte lévő kód ugyanaz,
//             mint a PC-s teszteken (VirtualBus). Az ESP32-C3 (OBD-egység) és az ESP32-S2
//             (ECU-szimulátor) is ezt használja, csak a lábkiosztás tér el.
//
// A régi (legacy) TWAI-drivert használja, mert az ESP-IDF 5.5-ben ez a kiforrott, és a
// szinkron fogadása közvetlenül illeszkedik a recv(timeout) felülethez. Az ICanPort mögött
// később az új esp_twai driverre is cserélhető, a felette lévő kód változtatása nélkül.
#include <cstdint>

#include "can_iface.h"

namespace og {

class TwaiBus : public ICanPort {
public:
    // Statisztika a diagnosztikához (a naplóban időnként kiírható).
    struct Stats {
        uint32_t tx_ok = 0;       // nyugtázott keretek
        uint32_t tx_fail = 0;     // nyugta nélkül maradt vagy el sem küldhető keretek
        uint32_t rx = 0;          // fogadott keretek
        uint32_t restarts = 0;    // a driver újraindításai (megszakított adás, bus-off)
    };

    // Mit csinál: eltárolja a lábakat és a napló címkéjét; a drivert még nem telepíti.
    // Hogyan:     a telepítés az első configure() hívásnál történik.
    // Miért:      így az objektum statikusan is létrehozható, és a sebességet mindig a
    //             hívó (pl. a protokoll-felismerés) választja meg.
    TwaiBus(int tx_gpio, int rx_gpio, const char* log_tag = "can");
    ~TwaiBus() override;

    TwaiBus(const TwaiBus&) = delete;
    TwaiBus& operator=(const TwaiBus&) = delete;

    bool configure(const CanConfig& cfg) override;
    bool send(const CanFrame& f, uint32_t timeout_ms) override;
    bool recv(CanFrame& f, uint32_t timeout_ms) override;
    uint32_t now_ms() override;

    // Keretnaplózás be/ki (alapértéke a CONFIG_OG_TWAI_LOG_FRAMES beállítás).
    void set_frame_log(bool on) { log_frames_ = on; }
    const Stats& stats() const { return stats_; }

private:
    bool install();
    void uninstall();
    void restart();
    void log_frame(const char* dir, const CanFrame& f, bool ok) const;

    int tx_gpio_;
    int rx_gpio_;
    const char* tag_;
    CanConfig cfg_;
    bool installed_ = false;
    bool log_frames_;
    Stats stats_;
};

}  // namespace og
