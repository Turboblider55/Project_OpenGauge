#pragma once
// Szerializálás / deszerializálás. Hardverfüggetlen, PC-n tesztelhető.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "og_protocol.h"

namespace og {

// Fejléc kitöltése (verzió + típus + sorszám).
Header make_header(MsgType type, uint16_t seq);

// Fejléc ellenőrzése nyers csomagból: hossz >= 4 és verzió egyezik.
// Siker esetén kitölti a hdr-t.
bool decode_header(const uint8_t* buf, size_t len, Header& hdr);

// Üzenet kiírása pufferbe. Visszatérés: írt bájtok száma, 0 ha a puffer kicsi.
template <class T>
size_t encode(const T& msg, uint8_t* buf, size_t cap) {
    if (buf == nullptr || cap < sizeof(T)) return 0;
    std::memcpy(buf, &msg, sizeof(T));
    return sizeof(T);
}

// Üzenet beolvasása: ellenőrzi a hosszt (pontos egyezés), a verziót és a típust.
template <class T>
bool decode(const uint8_t* buf, size_t len, T& out) {
    Header h;
    if (len != sizeof(T) || !decode_header(buf, len, h)) return false;
    if (h.type != static_cast<uint8_t>(T::kType)) return false;
    std::memcpy(&out, buf, sizeof(T));
    return true;
}

}  // namespace og
