#include "obd_client.h"

namespace og {

namespace {
inline uint32_t remaining_ms(uint32_t deadline, uint32_t now) {
    int32_t d = static_cast<int32_t>(deadline - now);
    return d > 0 ? static_cast<uint32_t>(d) : 0u;
}
}  // namespace

// Pozitív válasz akkor tartozik a kéréshez, ha a SID+0x40 egyezik, és az azonosító (PID
// vagy DID) is ugyanaz. Mode 03-nál nincs azonosító.
bool ObdClient::response_matches(const uint8_t* req, size_t req_len, const uint8_t* resp, size_t resp_len) const {
    if (resp_len < 1 || resp[0] != static_cast<uint8_t>(req[0] | 0x40)) return false;
    if (req[0] == kSidCurrentData || req[0] == kSidVehicleInfo) return resp_len >= 2 && req_len >= 2 && resp[1] == req[1];
    if (req[0] == kSidUdsReadDid) return resp_len >= 3 && req_len >= 3 && resp[1] == req[1] && resp[2] == req[2];
    return true;
}

Err ObdClient::request(const uint8_t* req, size_t req_len, uint8_t* resp, size_t cap, size_t& resp_len) {
    last_nrc_ = 0;
    resp_len = 0;
    tp_.drain();  // régi, késve érkezett válasz ne keveredjen bele
    Err e = (target_ == 0) ? tp_.send_functional(req, req_len) : tp_.send_physical(target_, req, req_len);
    if (e != Err::Ok) return e;

    uint32_t deadline = tp_.now_ms() + cfg_.p2_ms;
    for (;;) {
        size_t n = 0;
        uint32_t src = 0;
        e = tp_.receive(resp, cap, n, src, remaining_ms(deadline, tp_.now_ms()));
        if (e != Err::Ok) return e;  // Timeout, SequenceError, BufferTooSmall, ...

        // Fizikai célnál csak a kiválasztott ECU válasza érdekel.
        if (target_ != 0 && IsoTp::request_id_for(src, tp_.functional_id() == 0x18DB33F1u) != target_) continue;

        uint8_t nsid = 0, nrc = 0;
        if (obd_is_negative(resp, n, nsid, nrc)) {
            if (nsid != req[0]) continue;  // másik kérés negatív válasza
            if (nrc == kNrcResponsePending) {
                deadline = tp_.now_ms() + cfg_.p2_star_ms;  // az ECU kéri, hogy várjunk
                continue;
            }
            last_nrc_ = nrc;
            last_source_ = src;
            return Err::NegativeResponse;
        }
        if (!response_matches(req, req_len, resp, n)) continue;  // elavult/más válasz: eldobjuk
        resp_len = n;
        last_source_ = src;
        return Err::Ok;
    }
}

Err ObdClient::read_pid(uint8_t pid, uint8_t* data, size_t cap, size_t& len) {
    uint8_t req[2], resp[16];
    obd_build_mode01(pid, req);
    size_t n = 0;
    Err e = request(req, 2, resp, sizeof resp, n);
    if (e != Err::Ok) return e;
    const uint8_t* d = nullptr;
    size_t dl = 0;
    e = obd_parse_mode01(resp, n, pid, &d, dl);
    if (e != Err::Ok) return e;
    if (dl > cap) return Err::BufferTooSmall;
    for (size_t i = 0; i < dl; ++i) data[i] = d[i];
    len = dl;
    return Err::Ok;
}

Err ObdClient::read_supported(uint8_t base, uint32_t& bitmap) {
    uint8_t req[2], resp[16];
    obd_build_mode01(base, req);
    size_t n = 0;
    Err e = request(req, 2, resp, sizeof resp, n);
    if (e != Err::Ok) return e;
    return obd_parse_supported(resp, n, base, bitmap);
}

Err ObdClient::read_vin(char vin[18]) {
    uint8_t req[2], resp[32];  // a válasz 20 bájt; a puffer ráhagyással
    obd_build_mode09(0x02, req);
    size_t n = 0;
    Err e = request(req, 2, resp, sizeof resp, n);
    if (e != Err::Ok) return e;
    return obd_parse_vin(resp, n, vin);
}

Err ObdClient::read_dtc_count(uint8_t& count) {
    uint8_t req[1], resp[32];
    obd_build_mode03(req);
    size_t n = 0;
    Err e = request(req, 1, resp, sizeof resp, n);
    if (e != Err::Ok) return e;
    return obd_parse_dtc_count(resp, n, count);
}

}  // namespace og
