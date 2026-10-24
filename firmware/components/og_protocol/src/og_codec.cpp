#include "og_codec.h"

namespace og {

Header make_header(MsgType type, uint16_t seq) {
    Header h;
    h.ver = kProtoVersion;
    h.type = static_cast<uint8_t>(type);
    h.seq = seq;
    return h;
}

bool decode_header(const uint8_t* buf, size_t len, Header& hdr) {
    if (buf == nullptr || len < sizeof(Header)) return false;
    Header h;
    std::memcpy(&h, buf, sizeof(Header));
    if (h.ver != kProtoVersion) return false;
    hdr = h;
    return true;
}

}  // namespace og
