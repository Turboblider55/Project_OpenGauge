#include "telemetry_builder.h"

#include <cmath>

#include "og_codec.h"

namespace og {

namespace {

// Mit csinál: a PID friss értékét adja, ha van.
// Hogyan:     az ütemező értéke és életkora; friss, ha az életkor <= a periódus kétszerese.
// Miért:      egyetlen kimaradt válasz még nem teszi érvénytelenné az értéket, kettő már igen.
bool fresh(const PidScheduler& s, uint8_t pid, uint32_t now, float& v) {
    uint32_t age = 0;
    const uint32_t period = s.period(pid);
    if (period == 0 || !s.value(pid, v, now, age)) return false;
    return age <= 2 * period;
}

// Kerekítés a [lo, hi] tartományba szorítva.
long clamp_round(float v, long lo, long hi) {
    const long r = std::lround(v);
    return r < lo ? lo : (r > hi ? hi : r);
}

}  // namespace

TelemetryMsg build_telemetry(const PidScheduler& s, uint32_t now_ms, uint16_t seq) {
    TelemetryMsg m = {};
    m.h = make_header(MsgType::Telemetry, seq);
    float v = 0.f;
    if (fresh(s, 0x0C, now_ms, v)) {
        m.rpm_q4 = static_cast<uint16_t>(clamp_round(v * 4.f, 0, 65535));
        m.flags |= kFlagRpm;
    }
    if (fresh(s, 0x0D, now_ms, v)) {
        m.speed_kmh = static_cast<uint8_t>(clamp_round(v, 0, 255));
        m.flags |= kFlagSpeed;
    }
    if (fresh(s, 0x05, now_ms, v)) {
        m.coolant_c = static_cast<int16_t>(clamp_round(v, -40, 215));
        m.flags |= kFlagCoolant;
    }
    if (fresh(s, 0x11, now_ms, v)) {
        m.throttle_raw = static_cast<uint8_t>(clamp_round(v * 255.f / 100.f, 0, 255));
        m.flags |= kFlagThrottle;
    }
    if (fresh(s, 0x42, now_ms, v)) {
        m.vbat_mv = static_cast<uint16_t>(clamp_round(v * 1000.f, 0, 65535));
        m.flags |= kFlagVbat;
    }
    MilStatus mil;
    if (fresh(s, 0x01, now_ms, v) && s.mil(mil)) {
        m.mil_dtc = static_cast<uint8_t>((mil.mil_on ? 0x80 : 0x00) | (mil.dtc_count & 0x7F));
        m.flags |= kFlagMil;
    }
    return m;
}

}  // namespace og
