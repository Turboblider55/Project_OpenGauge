#include "isotp.h"

#include <cstring>

namespace og {

namespace {

constexpr uint8_t kPciSingle = 0x0;
constexpr uint8_t kPciFirst = 0x1;
constexpr uint8_t kPciConsecutive = 0x2;
constexpr uint8_t kPciFlowControl = 0x3;

constexpr uint8_t kFcContinue = 0x0;  // ContinueToSend
constexpr uint8_t kFcOverflow = 0x2;  // Overflow: a puffer kicsi

// Hátralévő idő egy határidőig (ms). Előjeles különbséggel számol, így a 32 bites
// időszámláló átfordulását is helyesen kezeli. Ha a határidő lejárt, 0.
inline uint32_t remaining_ms(uint32_t deadline, uint32_t now) {
    int32_t d = static_cast<int32_t>(deadline - now);
    return d > 0 ? static_cast<uint32_t>(d) : 0u;
}

}  // namespace

IsoTp::IsoTp(ICanBus& bus, const IsoTpConfig& cfg) : bus_(bus), cfg_(cfg) {}

uint32_t IsoTp::functional_id() const { return cfg_.ext_ids ? 0x18DB33F1u : 0x7DFu; }

// 11 bit: 0x7E8..0x7EF. 29 bit: 0x18DAF1xx (a felső 24 bit azonos, az alsó bájt az ECU címe).
bool IsoTp::is_response_id(uint32_t id, bool ext) {
    if (ext) return (id & 0x1FFFFF00u) == 0x18DAF100u;
    return id >= 0x7E8u && id <= 0x7EFu;
}

// A válasz-azonosítóból a hozzá tartozó fizikai kérés-azonosító:
//   11 bit: 0x7E8 + n -> 0x7E0 + n;  29 bit: 0x18DAF1xx -> 0x18DAxxF1.
// Az FC keretet erre a címre kell küldeni, mert az ECU ezt hallgatja.
uint32_t IsoTp::request_id_for(uint32_t response_id, bool ext) {
    if (ext) return 0x18DA0000u | ((response_id & 0xFFu) << 8) | 0xF1u;
    return 0x7E0u + (response_id - 0x7E8u);
}

Err IsoTp::send_single(uint32_t id, const uint8_t* data, size_t len) {
    // Az OBD-kérések (szolgáltatás + legfeljebb 6 paraméter) mindig egy keretbe férnek.
    if (data == nullptr || len == 0 || len > kIsoTpMaxSingleFrame) return Err::InvalidArg;
    CanFrame f;
    f.id = id;
    f.ext = cfg_.ext_ids;
    f.dlc = 8;  // az ISO 15765-4 szerint mindig 8 bájtos keret, kitöltéssel
    std::memset(f.data, 0x00, sizeof f.data);
    f.data[0] = static_cast<uint8_t>((kPciSingle << 4) | len);
    std::memcpy(&f.data[1], data, len);
    return bus_.send(f, cfg_.tx_timeout_ms) ? Err::Ok : Err::BusError;
}

Err IsoTp::send_functional(const uint8_t* data, size_t len) {
    return send_single(functional_id(), data, len);
}

Err IsoTp::send_physical(uint32_t request_id, const uint8_t* data, size_t len) {
    return send_single(request_id, data, len);
}

// Folyamvezérlő keret az ECU fizikai címére: [0x30 | jelző, BS, STmin].
Err IsoTp::send_flow_control(uint32_t source_id, uint8_t flag) {
    CanFrame f;
    f.id = request_id_for(source_id, cfg_.ext_ids);
    f.ext = cfg_.ext_ids;
    f.dlc = 8;
    std::memset(f.data, 0x00, sizeof f.data);
    f.data[0] = static_cast<uint8_t>((kPciFlowControl << 4) | flag);
    f.data[1] = cfg_.block_size;
    f.data[2] = cfg_.st_min_ms;
    return bus_.send(f, cfg_.tx_timeout_ms) ? Err::Ok : Err::BusError;
}

void IsoTp::drain() {
    CanFrame f;
    for (int i = 0; i < 64 && bus_.recv(f, 0); ++i) {
    }
}

Err IsoTp::receive(uint8_t* buf, size_t cap, size_t& len, uint32_t& source_id, uint32_t timeout_ms) {
    if (buf == nullptr) return Err::InvalidArg;

    // Állapot: addig "várakozunk" az első keretre, amíg FF-et nem látunk; utána a CF-eket
    // gyűjtjük a source_id azonosítójú ECU-tól.
    bool receiving = false;
    size_t total = 0, pos = 0;
    uint8_t expected_sn = 1;
    uint8_t frames_in_block = 0;
    uint32_t deadline = bus_.now_ms() + timeout_ms;
    source_id = 0;
    len = 0;

    for (;;) {
        CanFrame f;
        if (!bus_.recv(f, remaining_ms(deadline, bus_.now_ms()))) return Err::Timeout;

        // Csak az OBD-válasz azonosítók érdekelnek; a többi forgalmat (más vezérlők
        // üzenetei, a saját keretek) eldobjuk, de a határidő nem nyúlik meg.
        if (f.ext != cfg_.ext_ids || !is_response_id(f.id, f.ext) || f.dlc == 0) continue;
        const uint8_t pci = static_cast<uint8_t>(f.data[0] >> 4);

        if (!receiving) {
            if (pci == kPciSingle) {
                const size_t n = f.data[0] & 0x0F;
                if (n == 0 || n > kIsoTpMaxSingleFrame || n + 1u > f.dlc) return Err::ProtocolError;
                if (n > cap) return Err::BufferTooSmall;
                std::memcpy(buf, &f.data[1], n);
                len = n;
                source_id = f.id;
                return Err::Ok;
            }
            if (pci == kPciFirst) {
                total = (static_cast<size_t>(f.data[0] & 0x0F) << 8) | f.data[1];
                // Az FF hossza legalább 8 (különben egy keretbe fért volna), és az FF teli keret.
                if (total < 8 || f.dlc < 8) return Err::ProtocolError;
                source_id = f.id;
                if (total > cap) {
                    send_flow_control(source_id, kFcOverflow);  // jelezzük az ECU-nak: elég
                    return Err::BufferTooSmall;
                }
                std::memcpy(buf, &f.data[2], 6);
                pos = 6;
                expected_sn = 1;
                frames_in_block = 0;
                receiving = true;
                if (send_flow_control(source_id, kFcContinue) != Err::Ok) return Err::BusError;
                deadline = bus_.now_ms() + cfg_.n_cr_ms;  // innentől a CF-ek közti idő számít
                continue;
            }
            continue;  // elkóborolt CF vagy FC: a szabvány szerint figyelmen kívül hagyjuk
        }

        // Többkeretes fogadás közben: csak az FF-et küldő ECU folytatókeretei számítanak.
        if (f.id != source_id) continue;
        if (pci == kPciFlowControl) continue;
        if (pci != kPciConsecutive) return Err::SequenceError;  // új SF/FF a vétel közepén
        if ((f.data[0] & 0x0F) != expected_sn) return Err::SequenceError;

        size_t chunk = total - pos;
        if (chunk > 7) chunk = 7;
        if (chunk + 1u > f.dlc) return Err::ProtocolError;
        std::memcpy(buf + pos, &f.data[1], chunk);
        pos += chunk;
        expected_sn = static_cast<uint8_t>((expected_sn + 1) & 0x0F);
        if (pos >= total) {
            len = total;
            return Err::Ok;
        }
        // Blokkméret esetén a blokk végén újabb FC kell, különben az ECU leáll.
        if (cfg_.block_size != 0 && ++frames_in_block == cfg_.block_size) {
            frames_in_block = 0;
            if (send_flow_control(source_id, kFcContinue) != Err::Ok) return Err::BusError;
        }
        deadline = bus_.now_ms() + cfg_.n_cr_ms;
    }
}

}  // namespace og
