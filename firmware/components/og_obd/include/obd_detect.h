#pragma once
// Protokoll- és buszparaméter-felismerés (ISO 15765-4 + SAE J1979-2 4. ábra szerinti sorrend).
//
// Mit csinál: végigpróbálja a lehetséges CAN-beállításokat (500/250 kbit/s, 11/29 bit), és
//             megállapítja, hogy a jármű klasszikus J1979-et (Mode 01) vagy OBDonUDS-t
//             (J1979-2) beszél-e, mely ECU-k válaszolnak, és melyik PID-eket támogatják.
// Hogyan:     jelöltenként funkcionális 01 00 kérés; pozitív válasz (41 00) = klasszikus.
//             Ha nincs, funkcionális 22 F8 10; pozitív válasz (62 F8 10) = OBDonUDS.
//             Ha egyik sem, a következő jelölt jön.
// Miért:      a kettő kizárja egymást egy járműben, és a sebesség/azonosítóhossz előre nem
//             ismert. A tervezett eszköz az OBDonUDS-t nem kezeli: felismeri és jelzi.
//
// Megjegyzés: a keresés a buszra ír (kérés-kereteket küld), ezért rossz sebességnél a járműbusz
// hibakereteket láthat; ez minden hordozható OBD-olvasónál így van. A "csak hallgató" mód a
// CanConfig-ban elérhető, de a felismerés jelenleg nem használja.
#include <cstddef>
#include <cstdint>

#include "can_iface.h"
#include "og_err.h"

namespace og {

enum class ProtocolKind : uint8_t {
    None,       // nem találtunk választ
    Classic,    // SAE J1979, ISO 15765-4 (támogatott)
    ObdOnUds,   // SAE J1979-2 (felismerve, nem támogatott)
};

constexpr size_t kMaxEcus = 8;

struct ProtocolInfo {
    ProtocolKind kind = ProtocolKind::None;
    uint32_t bitrate_bps = 0;
    bool ext_ids = false;
    uint8_t ecu_count = 0;
    uint32_t ecu_response_id[kMaxEcus] = {0};  // a válaszoló ECU-k azonosítói
    uint32_t primary_request_id = 0;           // a kiválasztott (legkisebb azonosítójú) ECU fizikai kérés-azonosítója
    uint32_t supported[3] = {0, 0, 0};         // 01 00, 01 20, 01 40 bitmapek
    bool has_supported[3] = {false, false, false};
};

struct DetectConfig {
    uint32_t timeout_ms = 100;  // egy kérés válaszideje (P2CAN 50 ms + tartalék)
};

// Mit csinál: lefuttatja a felismerést; a sikeres beállítás a porton marad.
// Visszatérés: Ok (Classic), NotSupported (ObdOnUds; az info-ból a sebesség ismert),
//              Timeout (nincs egyik sem).
Err obd_detect(ICanPort& port, const DetectConfig& cfg, ProtocolInfo& out);

}  // namespace og
