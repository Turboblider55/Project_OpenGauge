#include "obd_detect.h"

#include "isotp.h"
#include "obd_client.h"
#include "obd_services.h"
#include "pid_table.h"

namespace og {

namespace {

struct Candidate {
    uint32_t bitrate;
    bool ext;
};
// Sorrend: a leggyakoribb (11 bit, 500 kbit/s) elöl; a 29 bites 500 kbit/s után 250 kbit/s.
constexpr Candidate kCandidates[] = {{500000, false}, {250000, false}, {500000, true}, {250000, true}};

inline uint32_t remaining_ms(uint32_t deadline, uint32_t now) {
    int32_t d = static_cast<int32_t>(deadline - now);
    return d > 0 ? static_cast<uint32_t>(d) : 0u;
}

// Funkcionális kérést küld, és a határidőig összegyűjti az összes (különböző) ECU pozitív
// válaszát. A pozitív válasz ellenőrzését a check függvény végzi. Visszaadja a találatok számát.
template <typename Check>
uint8_t probe(IsoTp& tp, const uint8_t* req, size_t req_len, uint32_t timeout_ms, ProtocolInfo& info, Check check) {
    tp.drain();
    if (tp.send_functional(req, req_len) != Err::Ok) return 0;  // nincs nyugtázás: nincs busz ezen a sebességen
    const uint32_t deadline = tp.now_ms() + timeout_ms;
    uint8_t found = 0;
    for (;;) {
        const uint32_t rem = remaining_ms(deadline, tp.now_ms());
        if (rem == 0) break;
        uint8_t buf[32];
        size_t n = 0;
        uint32_t src = 0;
        Err e = tp.receive(buf, sizeof buf, n, src, rem);
        if (e == Err::Timeout) break;
        if (e != Err::Ok || !check(buf, n)) continue;
        bool known = false;
        for (uint8_t i = 0; i < found; ++i)
            if (info.ecu_response_id[i] == src) known = true;
        if (!known && found < kMaxEcus) info.ecu_response_id[found++] = src;
    }
    return found;
}

}  // namespace

Err obd_detect(ICanPort& port, const DetectConfig& cfg, ProtocolInfo& out) {
    out = ProtocolInfo();
    for (const Candidate& c : kCandidates) {
        if (!port.configure(CanConfig{c.bitrate, false})) continue;
        IsoTpConfig tc;
        tc.ext_ids = c.ext;
        IsoTp tp(port, tc);

        ProtocolInfo info;
        info.bitrate_bps = c.bitrate;
        info.ext_ids = c.ext;

        // 1. lépés: klasszikus J1979 (01 00).
        uint8_t req[3];
        size_t rl = obd_build_mode01(0x00, req);
        info.ecu_count = probe(tp, req, rl, cfg.timeout_ms, info, [](const uint8_t* m, size_t n) {
            uint32_t bm;
            return obd_parse_supported(m, n, 0x00, bm) == Err::Ok;
        });
        if (info.ecu_count > 0) {
            info.kind = ProtocolKind::Classic;
            // Elsődleges ECU: a legkisebb válasz-azonosító (a motorvezérlő jellemzően 0x7E8).
            uint32_t primary = info.ecu_response_id[0];
            for (uint8_t i = 1; i < info.ecu_count; ++i)
                if (info.ecu_response_id[i] < primary) primary = info.ecu_response_id[i];
            info.primary_request_id = IsoTp::request_id_for(primary, c.ext);

            // Támogatott-PID bitmapek az elsődleges ECU-tól, fizikai címzéssel. A következő
            // blokkot csak akkor kérjük, ha az előző bitmap jelzi (a legalacsonyabb bit).
            ObdClient cl(tp);
            cl.set_target(info.primary_request_id);
            for (int b = 0; b < 3; ++b) {
                if (cl.read_supported(static_cast<uint8_t>(b * 0x20), info.supported[b]) != Err::Ok) break;
                info.has_supported[b] = true;
                if (!(info.supported[b] & 1u)) break;
            }
            out = info;
            return Err::Ok;
        }

        // 2. lépés: OBDonUDS (22 F8 10). A két protokoll kizárja egymást, ezért ide csak akkor
        // jutunk, ha a klasszikus kérésre nem jött válasz.
        rl = obd_build_uds_read_did(0xF810, req);
        info.ecu_count = probe(tp, req, rl, cfg.timeout_ms, info, [](const uint8_t* m, size_t n) {
            return obd_is_uds_protocol_id_response(m, n);
        });
        if (info.ecu_count > 0) {
            info.kind = ProtocolKind::ObdOnUds;
            out = info;
            return Err::NotSupported;
        }
    }
    return Err::Timeout;  // egyik jelölten sem válaszolt senki
}

}  // namespace og
