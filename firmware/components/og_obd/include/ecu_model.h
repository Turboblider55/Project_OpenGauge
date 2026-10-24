#pragma once
// Virtuális ECU: egy OBD-II (SAE J1979) motorvezérlő viselkedése, hardver nélkül.
//
// Mit csinál: kérés-keretekre válaszkereteket állít elő (Mode 01 PID-ek, támogatott-PID
//             bitmap, Mode 09 VIN többkeretes ISO-TP-vel, Mode 03, negatív válasz), valamint
//             "csak UDS" üzemmódban az OBDonUDS (J1979-2) felismerő kérésre válaszol.
// Hogyan:     az on_frame() kapja a buszról érkező kereteket, a válaszokat az Emitter
//             felületen keresztül, késleltetéssel adja ki (a késleltetés az ECU válaszideje).
// Miért:      ugyanezt a modellt használja (1) a PC-s teszt a VirtualBus-on, és (2) az
//             ESP32-S2-es ECU-szimulátor valós CAN-buszon, így a kettő viselkedése azonos.
//
// Egyszerűsítések (szándékosak): a válaszidő állandó; a hibakódok (DTC) kitaláltak; a
// negatív válasz csak 0x11 (service not supported); az ECU csak a saját fizikai címére és a
// funkcionális címre figyel.
#include <cstddef>
#include <cstdint>

#include "can_iface.h"

namespace og {

// A járműállapot, amelyet az ECU a PID-válaszokban visszaad. A tesztek és a szimulátor
// futás közben is módosíthatják (pl. változó fordulatszám).
struct VehicleState {
    float rpm = 850.f;
    float speed_kmh = 0.f;
    float coolant_c = 90.f;
    float throttle_pct = 15.f;
    float vbat_v = 14.1f;
    bool mil_on = false;
    uint8_t dtc_count = 0;
    char vin[18] = "WTEST12345678901A";  // 17 karakter + lezáró; I, O, Q nélkül (ISO 3779)
};

enum class EcuProtocol : uint8_t {
    Classic,  // SAE J1979 (Mode 01/03/09)
    UdsOnly,  // OBDonUDS (J1979-2): a klasszikus 01 00 kérésre nem válaszol
};

struct EcuConfig {
    bool ext_ids = false;          // 29 bites azonosítók
    uint8_t index = 0;             // 0..7: a válasz-azonosító 0x7E8+index / 0x18DAF1(0xE8+index)
    uint32_t latency_ms = 40;      // válaszidő (a kérés vétele és a válasz között)
    uint8_t st_min_ms = 0;         // az ECU saját keretköze többkeretes küldésnél
    EcuProtocol protocol = EcuProtocol::Classic;
};

// A kimenő keretek átvevője. A busz (VirtualBus) vagy a valós CAN-driver oldja meg.
class Emitter {
public:
    virtual ~Emitter() = default;
    // A keretet delay_ms múlva kell a buszra tenni.
    virtual void emit(const CanFrame& f, uint32_t delay_ms) = 0;
};

class EcuModel {
public:
    explicit EcuModel(const EcuConfig& cfg = EcuConfig());

    VehicleState state;  // szabadon módosítható

    // Mit csinál: egy buszon látott keretet feldolgoz, és szükség esetén választ ad.
    // Hogyan:     azonosító- és típusszűrés, majd SF -> kérés, FC -> folytatókeretek küldése.
    // Miért:      a busz minden keretet minden szereplőnek továbbít, az ECU dönti el, mi neki szól.
    void on_frame(const CanFrame& rx, Emitter& out);

    // Azonosítók (ISO 15765-4).
    uint32_t response_id() const;
    uint32_t request_id() const;  // fizikai kérés-azonosító (erre figyel)

    const EcuConfig& config() const { return cfg_; }

    // A támogatott-PID bitmap előállítása (public: a tesztek összevetik a kliens olvasatával).
    // base = 0x00/0x20/0x40/...; a "következő blokk" bitet akkor állítja, ha van nagyobb PID.
    static uint32_t supported_bitmap(uint8_t base);

private:
    void handle_request(const uint8_t* req, size_t n, bool functional, Emitter& out);
    void send_message(const uint8_t* payload, size_t len, Emitter& out);
    void send_cf_block(uint8_t block_size, Emitter& out);
    void send_negative(uint8_t sid, uint8_t nrc, Emitter& out);

    EcuConfig cfg_;
    // Folyamatban lévő többkeretes küldés (FF elment, FC-re várunk).
    uint8_t pend_[256];
    size_t pend_len_ = 0, pend_pos_ = 0;
    uint8_t pend_sn_ = 1;
    bool pending_ = false;
};

}  // namespace og
