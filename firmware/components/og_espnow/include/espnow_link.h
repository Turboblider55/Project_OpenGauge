#pragma once
// ESP-NOW rádiókapcsolat a két egység között (ESP-IDF-függő illesztő).
//
// Mit csinál: elindítja a Wi-Fi-t állomás módban rögzített csatornán (hozzáférési pont nélkül),
//             csomagot küld a beállított társnak, és a vett csomagokat sorba teszi.
// Hogyan:     esp_now_send a küldéshez; a vételi visszahívás (a Wi-Fi-feladatban fut) csak
//             bemásolja a csomagot egy FreeRTOS-sorba, a feldolgozás a hívó feladatában történik.
//             A társ MAC-címe a menuconfigból jön (alapértéke a szórási cím).
// Miért:      a vételi visszahívásban nem szabad hosszan dolgozni (a Wi-Fi-feladatot blokkolná);
//             a sor leválasztja a rádiót a feldolgozásról. Az illesztő az üzenet tartalmát nem
//             ismeri, a kódolás az og_protocol dolga (hardverfüggetlen, PC-n tesztelhető).
//
// Szórási címre (FF:FF:FF:FF:FF:FF) küldve nincs MAC-szintű nyugta és ismétlés: a küldés
// mindig "sikeres", a veszteség csak a vevő sorszámaiból látszik. Konkrét MAC-címre küldve a
// rádió nyugtát vár és ismétel; ehhez a társ címét kell megadni (később párosítással, S-20).
#include <cstddef>
#include <cstdint>

#include "og_protocol.h"

namespace og {

class EspNowLink {
public:
    struct Packet {
        uint8_t src[6];
        int8_t rssi;       // dBm
        uint8_t len;
        uint32_t rx_ms;    // vétel ideje (esp_timer, ms)
        uint8_t data[kEspNowMaxPayload];
    };

    struct Stats {
        uint32_t tx_ok;       // a rádió szerint elküldve (szórásnál mindig)
        uint32_t tx_fail;     // a rádió szerint sikertelen vagy el sem indult
        uint32_t rx;          // sorba tett vett csomagok
        uint32_t rx_dropped;  // tele volt a sor
    };

    // Mit csinál: NVS, Wi-Fi (STA, rögzített csatorna, energiatakarékosság nélkül) és ESP-NOW
    //             indítása, a társ felvétele. peer = nullptr esetén a menuconfig szerinti cím.
    // Miért:      az energiatakarékos mód kikapcsolása a vevőnél kell: különben a rádió alszik,
    //             és a csomagok egy része elvész.
    bool init(uint8_t channel, const uint8_t* peer = nullptr);

    // Mit csinál: egy csomagot küld a társnak. Hamis, ha a küldés el sem indult.
    bool send(const void* data, size_t len);

    // Mit csinál: legfeljebb timeout_ms-ig vár egy vett csomagra.
    bool recv(Packet& p, uint32_t timeout_ms);

    Stats stats() const;
    void own_mac(uint8_t out[6]) const;
    const uint8_t* peer() const { return peer_; }

private:
    uint8_t peer_[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool ready_ = false;
};

}  // namespace og
