#pragma once
// ISO 15765-2 (ISO-TP) szállítási réteg, az OBD-II diagnosztika kliens (teszter) oldalához.
//
// Mit csinál: egykeretes kérést küld, és egy- vagy többkeretes választ fogad (SF/FF/CF),
//             a többkeretes fogadáshoz folyamvezérlő (FC) kereteket küld.
// Hogyan:     az OBD-kérések mindig legfeljebb 7 bájtosak, ezért küldeni csak egykeretest
//             kell; fogadáskor állapotgép követi az első keretet (FF) és a folytatókat (CF).
// Miért:      a VIN-válasz (20 bájt) és a hibakód-lista nem fér egy keretbe.
//
// Az azonosítókiosztás az ISO 15765-4 szerinti:
//   11 bit: funkcionális kérés 0x7DF; fizikai kérés 0x7E0..0x7E7; válasz 0x7E8..0x7EF
//   29 bit: funkcionális kérés 0x18DB33F1; fizikai kérés 0x18DAxxF1; válasz 0x18DAF1xx
#include <cstddef>
#include <cstdint>

#include "can_iface.h"
#include "og_err.h"

namespace og {

constexpr size_t kIsoTpMaxMessage = 4095;  // az ISO-TP 12 bites hosszmezője
constexpr size_t kIsoTpMaxSingleFrame = 7; // egykeretes üzenet legfeljebb 7 adatbájt

struct IsoTpConfig {
    bool ext_ids = false;       // igaz: 29 bites azonosítók
    uint8_t block_size = 0;     // az FC-ben kért blokkméret (0: az ECU egyben küldhet)
    uint8_t st_min_ms = 0;      // az FC-ben kért minimális keretköz (ms)
    uint32_t n_cr_ms = 150;     // két folytatókeret közti legnagyobb várakozás (ISO 15765-4)
    uint32_t tx_timeout_ms = 10;// egy keret adásának időkorlátja
};

class IsoTp {
public:
    IsoTp(ICanBus& bus, const IsoTpConfig& cfg = IsoTpConfig());

    // Mit csinál: a funkcionális kérés azonosítóját adja (11 bit: 0x7DF, 29 bit: 0x18DB33F1).
    // Hogyan:     a beállított azonosítóhossz alapján.
    // Miért:      a funkcionális kérésre minden ECU válaszolhat; a felderítéshez ez kell.
    uint32_t functional_id() const;

    // Mit csinál: egykeretes kérést küld funkcionális címre.
    // Hogyan:     [hossz, adatok..., 0 kitöltés] 8 bájtos keretben (az ISO 15765-4 DLC = 8).
    // Miért:      protokoll-felismeréskor és az első kérésekkor még nem ismerjük az ECU-t.
    Err send_functional(const uint8_t* data, size_t len);

    // Mit csinál: egykeretes kérést küld egy adott ECU fizikai azonosítójára.
    // Hogyan:     ugyanúgy, mint a funkcionális, csak más azonosítóval.
    // Miért:      a felderítés után csak a kiválasztott ECU szólal meg, így nincs
    //             többszörös válasz, és a lekérdezés gyorsabb.
    Err send_physical(uint32_t request_id, const uint8_t* data, size_t len);

    // Mit csinál: egy teljes üzenetet fogad (egy- vagy többkeretest).
    // Hogyan:     az elsőként megszólaló válasz-azonosítót követi; többkeretesnél FC-t küld,
    //             ellenőrzi a sorszámot és az időzítést; a más azonosítójú vagy nem OBD
    //             forgalmat figyelmen kívül hagyja. timeout_ms az első keretre vonatkozik.
    // Miért:      a hívó (ObdClient) egyetlen hívással kapja meg a teljes választ.
    // Hibák:      Timeout (nincs keret), SequenceError, BufferTooSmall, ProtocolError.
    Err receive(uint8_t* buf, size_t cap, size_t& len, uint32_t& source_id, uint32_t timeout_ms);

    // Mit csinál: eldobja a buszon már várakozó, elavult kereteket.
    // Hogyan:     nulla időkorláttal olvas, amíg van keret (legfeljebb 64-et).
    // Miért:      egy késve érkezett régi válasz ne keveredjen a következő kérés válaszába.
    void drain();

    // A busz ideje (ms); a kliens határidői ezt használják.
    uint32_t now_ms() { return bus_.now_ms(); }

    // Az azonosítók kiosztása (ISO 15765-4). Statikusak, hogy külön is tesztelhetők legyenek.
    static bool is_response_id(uint32_t id, bool ext);
    static uint32_t request_id_for(uint32_t response_id, bool ext);

private:
    Err send_single(uint32_t id, const uint8_t* data, size_t len);
    Err send_flow_control(uint32_t source_id, uint8_t flag);

    ICanBus& bus_;
    IsoTpConfig cfg_;
};

}  // namespace og
