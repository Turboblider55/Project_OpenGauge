#pragma once
#include <cstdint>

namespace og {

// Elveszett csomagok száma két egymást követő vett sorszám között
// (16 bites átfordulással): (cur - prev - 1) mod 65536.
inline uint16_t seq_gap(uint16_t prev, uint16_t cur) {
    return static_cast<uint16_t>(cur - prev - 1);
}

// Igaz, ha a cur újabb, mint a prev (a félkörös összehasonlítás kezeli az átfordulást).
// Hamis duplikátumnál és késve érkező (régebbi) csomagnál.
inline bool seq_is_newer(uint16_t prev, uint16_t cur) {
    return static_cast<int16_t>(static_cast<uint16_t>(cur - prev)) > 0;
}

}  // namespace og
