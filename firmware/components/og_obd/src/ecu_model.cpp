#include "ecu_model.h"

#include <cstring>

#include "pid_table.h"

namespace og {

namespace {
// A szimulált ECU által támogatott Mode 01 PID-ek (a pid_table.cpp táblájával azonos készlet).
constexpr uint8_t kSupportedPids[] = {0x01, 0x05, 0x0C, 0x0D, 0x11, 0x42};
bool is_supported(uint8_t pid) {
    for (uint8_t p : kSupportedPids)
        if (p == pid) return true;
    return false;
}
}  // namespace

EcuModel::EcuModel(const EcuConfig& cfg) : cfg_(cfg) { std::memset(pend_, 0, sizeof pend_); }

// 11 bit: 0x7E8 + index. 29 bit: 0x18DAF1xx, ahol xx = 0xE8 + index (az ECU forrás-címe).
uint32_t EcuModel::response_id() const {
    return cfg_.ext_ids ? (0x18DAF100u | (0xE8u + cfg_.index)) : (0x7E8u + cfg_.index);
}
// A fizikai kérés: 11 bit 0x7E0 + index; 29 bit 0x18DAxxF1 (xx = az ECU címe).
uint32_t EcuModel::request_id() const {
    return cfg_.ext_ids ? (0x18DA00F1u | ((0xE8u + cfg_.index) << 8)) : (0x7E0u + cfg_.index);
}

// A bitmap bitje a PID-ekhez: a legmagasabb helyiértékű bit a base+1 PID (J1979 szerint).
// Ha a kért blokknál nagyobb PID is létezik, a base+0x20 bit is be van állítva,
// így a kliens tudja, hogy érdemes a következő blokkot is lekérdezni.
uint32_t EcuModel::supported_bitmap(uint8_t base) {
    uint32_t bm = 0;
    bool higher = false;
    for (uint8_t p : kSupportedPids) {
        if (p > base + 0x20) higher = true;
        if (p > base && p <= base + 0x20) bm |= 1u << (32 - (p - base));
    }
    if (higher) bm |= 1u;  // a base+0x20 PID bitje (a legalacsonyabb helyiértékű)
    return bm;
}

void EcuModel::send_negative(uint8_t sid, uint8_t nrc, Emitter& out) {
    const uint8_t m[3] = {0x7F, sid, nrc};
    send_message(m, 3, out);
}

// Üzenet küldése ISO-TP-vel. Egykeretes, ha belefér; különben FF-et küld, és a maradékot
// eltárolja, amíg a tesztertől FC nem érkezik (az ISO-TP így működik: a fogadó engedélyez).
void EcuModel::send_message(const uint8_t* payload, size_t len, Emitter& out) {
    CanFrame f;
    f.id = response_id();
    f.ext = cfg_.ext_ids;
    f.dlc = 8;
    std::memset(f.data, 0x00, 8);
    if (len <= 7) {
        f.data[0] = static_cast<uint8_t>(len);
        std::memcpy(&f.data[1], payload, len);
        out.emit(f, cfg_.latency_ms);
        return;
    }
    if (len > sizeof pend_) return;  // a szimulátor nem küld ekkora üzenetet
    f.data[0] = static_cast<uint8_t>(0x10 | ((len >> 8) & 0x0F));
    f.data[1] = static_cast<uint8_t>(len & 0xFF);
    std::memcpy(&f.data[2], payload, 6);
    std::memcpy(pend_, payload, len);
    pend_len_ = len;
    pend_pos_ = 6;
    pend_sn_ = 1;
    pending_ = true;
    out.emit(f, cfg_.latency_ms);
}

// Folytatókeretek küldése FC után: block_size db (0 = az összes). A keretek között az ECU
// saját st_min_ms keretköze telik el. Blokkvégen újabb FC-re vár.
void EcuModel::send_cf_block(uint8_t block_size, Emitter& out) {
    uint32_t delay = 0;
    uint8_t sent = 0;
    while (pending_ && pend_pos_ < pend_len_) {
        CanFrame f;
        f.id = response_id();
        f.ext = cfg_.ext_ids;
        f.dlc = 8;
        std::memset(f.data, 0x00, 8);
        f.data[0] = static_cast<uint8_t>(0x20 | (pend_sn_ & 0x0F));
        size_t chunk = pend_len_ - pend_pos_;
        if (chunk > 7) chunk = 7;
        std::memcpy(&f.data[1], pend_ + pend_pos_, chunk);
        pend_pos_ += chunk;
        pend_sn_ = static_cast<uint8_t>((pend_sn_ + 1) & 0x0F);
        out.emit(f, delay);
        delay += cfg_.st_min_ms;
        ++sent;
        if (pend_pos_ >= pend_len_) pending_ = false;
        if (block_size != 0 && sent == block_size) break;
    }
}

void EcuModel::handle_request(const uint8_t* req, size_t n, bool functional, Emitter& out) {
    if (n == 0) return;
    const uint8_t sid = req[0];

    if (cfg_.protocol == EcuProtocol::UdsOnly) {
        // OBDonUDS: csak az 22 F8 10 (protokoll-azonosító) felismerő kérésre válaszol.
        // A válasz tartalma (utolsó bájt) a szimulátorban kitalált; a felismerés csak
        // azt nézi, hogy pozitív 62 F8 10 válasz érkezik-e.
        if (sid == 0x22 && n >= 3 && req[1] == 0xF8 && req[2] == 0x10) {
            const uint8_t m[4] = {0x62, 0xF8, 0x10, 0x01};
            send_message(m, 4, out);
        }
        return;  // a többi kérésre (pl. 01 00) nem válaszol: a két protokoll kizárja egymást
    }

    switch (sid) {
        case 0x01: {  // aktuális adatok
            if (n < 2) return;
            const uint8_t pid = req[1];
            uint8_t m[8] = {0x41, pid};
            if (pid == 0x00 || pid == 0x20 || pid == 0x40 || pid == 0x60) {
                const uint32_t bm = supported_bitmap(pid);
                m[2] = static_cast<uint8_t>(bm >> 24);
                m[3] = static_cast<uint8_t>(bm >> 16);
                m[4] = static_cast<uint8_t>(bm >> 8);
                m[5] = static_cast<uint8_t>(bm);
                send_message(m, 6, out);
                return;
            }
            if (!is_supported(pid)) return;  // J1979: nem támogatott PID-re nincs válasz
            size_t k = 0;
            switch (pid) {
                case 0x01:  // A: MIL (7. bit) + hibakódok száma; B, C, D: monitorok (nem szimuláljuk)
                    m[2] = static_cast<uint8_t>((state.mil_on ? 0x80 : 0x00) | (state.dtc_count & 0x7F));
                    m[3] = m[4] = m[5] = 0;
                    k = 4;
                    break;
                case 0x05: k = pid_encode(pid, state.coolant_c, &m[2], 6); break;
                case 0x0C: k = pid_encode(pid, state.rpm, &m[2], 6); break;
                case 0x0D: k = pid_encode(pid, state.speed_kmh, &m[2], 6); break;
                case 0x11: k = pid_encode(pid, state.throttle_pct, &m[2], 6); break;
                case 0x42: k = pid_encode(pid, state.vbat_v, &m[2], 6); break;
                default: return;
            }
            if (k == 0) return;
            send_message(m, 2 + k, out);
            return;
        }
        case 0x03: {  // tárolt hibakódok: 43, darabszám, majd 2 bájt/kód (kitalált P0100...)
            uint8_t m[2 + 2 * 8];
            const uint8_t cnt = state.dtc_count > 8 ? 8 : state.dtc_count;
            m[0] = 0x43;
            m[1] = cnt;
            for (uint8_t i = 0; i < cnt; ++i) {
                m[2 + 2 * i] = 0x01;
                m[3 + 2 * i] = static_cast<uint8_t>(i);
            }
            send_message(m, 2 + 2 * cnt, out);
            return;
        }
        case 0x09: {  // jármű-információ
            if (n < 2) return;
            if (req[1] == 0x00) {  // csak a 02 (VIN) támogatott: a 2. PID bitje
                const uint8_t m[6] = {0x49, 0x00, 0x40, 0x00, 0x00, 0x00};
                send_message(m, 6, out);
            } else if (req[1] == 0x02) {  // 49 02 01 + 17 karakter = 20 bájt: többkeretes
                uint8_t m[20] = {0x49, 0x02, 0x01};
                std::memcpy(&m[3], state.vin, 17);
                send_message(m, 20, out);
            }
            return;
        }
        default:
            // Fizikai címzésre negatív választ adunk; funkcionálisra hallgatunk (ISO 14229-1
            // szerint a funkcionális "nem támogatott" kérést nem szabad megválaszolni).
            if (!functional) send_negative(sid, 0x11, out);
            return;
    }
}

void EcuModel::on_frame(const CanFrame& rx, Emitter& out) {
    if (rx.ext != cfg_.ext_ids || rx.dlc == 0) return;
    const uint32_t func_id = cfg_.ext_ids ? 0x18DB33F1u : 0x7DFu;
    const bool functional = (rx.id == func_id);
    if (!functional && rx.id != request_id()) return;

    const uint8_t pci = static_cast<uint8_t>(rx.data[0] >> 4);
    if (pci == 0x0) {  // egykeretes kérés
        const size_t n = rx.data[0] & 0x0F;
        if (n >= 1 && n <= 7 && n + 1u <= rx.dlc) handle_request(&rx.data[1], n, functional, out);
    } else if (pci == 0x3 && !functional && pending_) {  // folyamvezérlés a teszterről
        const uint8_t fs = rx.data[0] & 0x0F;
        if (fs == 0) {
            send_cf_block(rx.dlc > 1 ? rx.data[1] : 0, out);
        } else if (fs == 2) {
            pending_ = false;  // Overflow: a teszter nem kéri a többit
        }
        // fs == 1 (Wait): a modell nem küld tovább, a következő FC-t várja
    }
}

}  // namespace og
