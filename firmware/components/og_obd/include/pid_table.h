#pragma once
// SAE J1979 Mode 01 PID-ek: leírás, dekódolás, kódolás (az ECU-szimulátorhoz).
#include <cstddef>
#include <cstdint>

namespace og {

struct PidInfo {
    uint8_t pid;
    const char* name;
    uint8_t data_len;  // válasz adatbájtjainak száma (A, B, ...)
};

// nullptr, ha a PID nincs a táblában.
const PidInfo* pid_find(uint8_t pid);

// A válasz adatbájtjaiból fizikai érték. Egységek:
//   0x05 hűtővíz [°C], 0x0C fordulatszám [1/perc], 0x0D sebesség [km/h],
//   0x11 fojtószelep [%], 0x42 modulfeszültség [V].
// false, ha ismeretlen a PID vagy kevés az adatbájt.
bool pid_decode(uint8_t pid, const uint8_t* data, size_t len, float& out);

// Fordított irány (ECU-szimulátor). Visszatérés: írt bájtok száma, 0 hiba esetén.
size_t pid_encode(uint8_t pid, float value, uint8_t* out, size_t cap);

// PID 01 01, A bájt: MIL-állapot és hibakódok száma.
struct MilStatus {
    bool mil_on;
    uint8_t dtc_count;
};
MilStatus decode_mil_status(uint8_t a);

// Támogatott-PID bitmap (01 00 / 01 20 / 01 40 válasz, 4 bájt big-endian egészként).
// pid_base: 0x00, 0x20, 0x40. Igaz, ha a pid (pid_base+1 … pid_base+0x20) támogatott.
bool pid_in_bitmap(uint32_t bitmap, uint8_t pid_base, uint8_t pid);

}  // namespace og
