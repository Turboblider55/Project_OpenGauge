#include "pid_table.h"

#include <cmath>

namespace og {

static const PidInfo kPids[] = {
    {0x01, "Monitor status (MIL, DTC)", 4},
    {0x05, "Coolant temperature", 1},
    {0x0C, "Engine speed", 2},
    {0x0D, "Vehicle speed", 1},
    {0x11, "Throttle position", 1},
    {0x42, "Control module voltage", 2},
};

const PidInfo* pid_find(uint8_t pid) {
    for (const auto& p : kPids)
        if (p.pid == pid) return &p;
    return nullptr;
}

static uint8_t clamp_u8(float v) {
    if (v < 0.f) return 0;
    if (v > 255.f) return 255;
    return static_cast<uint8_t>(std::lround(v));
}

static uint16_t clamp_u16(float v) {
    if (v < 0.f) return 0;
    if (v > 65535.f) return 65535;
    return static_cast<uint16_t>(std::lround(v));
}

bool pid_decode(uint8_t pid, const uint8_t* d, size_t len, float& out) {
    const PidInfo* info = pid_find(pid);
    if (info == nullptr || d == nullptr || len < info->data_len) return false;
    switch (pid) {
        case 0x05: out = static_cast<float>(d[0]) - 40.f; return true;
        case 0x0C: out = (256.f * d[0] + d[1]) / 4.f; return true;
        case 0x0D: out = static_cast<float>(d[0]); return true;
        case 0x11: out = d[0] * 100.f / 255.f; return true;
        case 0x42: out = (256.f * d[0] + d[1]) / 1000.f; return true;
        default: return false;  // pl. 0x01: lásd decode_mil_status
    }
}

size_t pid_encode(uint8_t pid, float v, uint8_t* out, size_t cap) {
    switch (pid) {
        case 0x05:
            if (cap < 1) return 0;
            out[0] = clamp_u8(v + 40.f);
            return 1;
        case 0x0C: {
            if (cap < 2) return 0;
            uint16_t raw = clamp_u16(v * 4.f);
            out[0] = static_cast<uint8_t>(raw >> 8);
            out[1] = static_cast<uint8_t>(raw & 0xFF);
            return 2;
        }
        case 0x0D:
            if (cap < 1) return 0;
            out[0] = clamp_u8(v);
            return 1;
        case 0x11:
            if (cap < 1) return 0;
            out[0] = clamp_u8(v * 255.f / 100.f);
            return 1;
        case 0x42: {
            if (cap < 2) return 0;
            uint16_t raw = clamp_u16(v * 1000.f);
            out[0] = static_cast<uint8_t>(raw >> 8);
            out[1] = static_cast<uint8_t>(raw & 0xFF);
            return 2;
        }
        default: return 0;
    }
}

MilStatus decode_mil_status(uint8_t a) {
    return MilStatus{(a & 0x80) != 0, static_cast<uint8_t>(a & 0x7F)};
}

bool pid_in_bitmap(uint32_t bitmap, uint8_t pid_base, uint8_t pid) {
    if (pid <= pid_base || pid > static_cast<uint8_t>(pid_base + 0x20)) return false;
    return (bitmap >> (32 - (pid - pid_base))) & 1u;
}

}  // namespace og
