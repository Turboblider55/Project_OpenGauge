#include "obd_services.h"

namespace og {

size_t obd_build_mode01(uint8_t pid, uint8_t* out) {
    out[0] = kSidCurrentData;
    out[1] = pid;
    return 2;
}
size_t obd_build_mode09(uint8_t pid, uint8_t* out) {
    out[0] = kSidVehicleInfo;
    out[1] = pid;
    return 2;
}
size_t obd_build_mode03(uint8_t* out) {
    out[0] = kSidStoredDtc;
    return 1;
}
size_t obd_build_uds_read_did(uint16_t did, uint8_t* out) {
    out[0] = kSidUdsReadDid;
    out[1] = static_cast<uint8_t>(did >> 8);
    out[2] = static_cast<uint8_t>(did & 0xFF);
    return 3;
}

bool obd_is_negative(const uint8_t* msg, size_t len, uint8_t& sid, uint8_t& nrc) {
    if (len < 3 || msg[0] != 0x7F) return false;
    sid = msg[1];
    nrc = msg[2];
    return true;
}

Err obd_parse_mode01(const uint8_t* msg, size_t len, uint8_t pid, const uint8_t** data, size_t& data_len) {
    if (len < 2) return Err::BadLength;
    if (msg[0] != (kSidCurrentData | 0x40) || msg[1] != pid) return Err::ProtocolError;
    if (len < 3) return Err::BadLength;
    *data = msg + 2;
    data_len = len - 2;
    return Err::Ok;
}

Err obd_parse_supported(const uint8_t* msg, size_t len, uint8_t base, uint32_t& bitmap) {
    const uint8_t* d = nullptr;
    size_t n = 0;
    Err e = obd_parse_mode01(msg, len, base, &d, n);
    if (e != Err::Ok) return e;
    if (n < 4) return Err::BadLength;
    bitmap = (static_cast<uint32_t>(d[0]) << 24) | (static_cast<uint32_t>(d[1]) << 16) |
             (static_cast<uint32_t>(d[2]) << 8) | d[3];
    return Err::Ok;
}

Err obd_parse_vin(const uint8_t* msg, size_t len, char out[18]) {
    // 49 02 01 + 17 karakter. Egyes autók kitöltő 0x00 bájtokkal küldik: azt itt nem fogadjuk el.
    if (len != 20) return Err::BadLength;
    if (msg[0] != 0x49 || msg[1] != 0x02 || msg[2] != 0x01) return Err::ProtocolError;
    for (int i = 0; i < 17; ++i) {
        const char c = static_cast<char>(msg[3 + i]);
        const bool ok = ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) && c != 'I' && c != 'O' && c != 'Q';
        if (!ok) return Err::ProtocolError;
        out[i] = c;
    }
    out[17] = '\0';
    return Err::Ok;
}

Err obd_parse_dtc_count(const uint8_t* msg, size_t len, uint8_t& count) {
    if (len < 2) return Err::BadLength;
    if (msg[0] != 0x43) return Err::ProtocolError;
    count = msg[1];
    return Err::Ok;
}

bool obd_is_uds_protocol_id_response(const uint8_t* msg, size_t len) {
    return len >= 3 && msg[0] == 0x62 && msg[1] == 0xF8 && msg[2] == 0x10;
}

}  // namespace og
