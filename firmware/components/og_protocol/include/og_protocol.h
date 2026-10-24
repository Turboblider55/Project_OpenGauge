#pragma once
// OpenGauge ESP-NOW üzenetformátum (little-endian, packed). Verzió: 1.
// Az ESP32-C3 és az ESP32-S3 is little-endian, ezért a szerializálás memcpy.
#include <cstddef>
#include <cstdint>

namespace og {

constexpr uint8_t kProtoVersion = 1;
constexpr size_t kEspNowMaxPayload = 250;

enum class MsgType : uint8_t {
    Telemetry = 0x01,
    Vin = 0x02,
    Ready = 0x10,
    Heartbeat = 0x11,
    Request = 0x12,
    Sleep = 0x13,
};

// TelemetryMsg::flags bitjei: mely mezők érvényesek (frissek és támogatottak).
enum TelemetryFlag : uint16_t {
    kFlagRpm = 1u << 0,
    kFlagSpeed = 1u << 1,
    kFlagCoolant = 1u << 2,
    kFlagThrottle = 1u << 3,
    kFlagVbat = 1u << 4,
    kFlagMil = 1u << 5,
};

// RequestMsg::code értékei (kijelző -> OBD-egység).
enum class RequestCode : uint8_t {
    Vin = 0,
    ReadDtc = 1,  // csak olvasás; a hibakódok törlése nem része a projektnek
};

#pragma pack(push, 1)

struct Header {
    uint8_t ver;
    uint8_t type;
    uint16_t seq;
};

struct TelemetryMsg {
    static constexpr MsgType kType = MsgType::Telemetry;
    Header h;
    uint16_t flags;
    uint16_t rpm_q4;       // 1/4 ford/perc (nyers OBD érték)
    uint8_t speed_kmh;
    int16_t coolant_c;     // °C
    uint8_t throttle_raw;  // 0..255 -> 0..100 %
    uint16_t vbat_mv;      // PID 0x42
    uint8_t mil_dtc;       // bit7 = MIL, bit0..6 = hibakódok száma (PID 01 01, A bájt)
};

struct VinMsg {
    static constexpr MsgType kType = MsgType::Vin;
    Header h;
    char vin[18];  // 17 ASCII karakter + lezáró nulla
};

struct ReadyMsg {
    static constexpr MsgType kType = MsgType::Ready;
    Header h;
    uint8_t role;        // 1 = kijelző
    uint8_t fw_version;
};

struct HeartbeatMsg {
    static constexpr MsgType kType = MsgType::Heartbeat;
    Header h;
    uint32_t uptime_s;
};

struct RequestMsg {
    static constexpr MsgType kType = MsgType::Request;
    Header h;
    uint8_t code;  // RequestCode
    uint8_t arg;
};

struct SleepMsg {
    static constexpr MsgType kType = MsgType::Sleep;
    Header h;
    uint8_t reason;
};

#pragma pack(pop)

static_assert(sizeof(Header) == 4, "Header");
static_assert(sizeof(TelemetryMsg) == 15, "TelemetryMsg");
static_assert(sizeof(VinMsg) == 22, "VinMsg");
static_assert(sizeof(ReadyMsg) == 6, "ReadyMsg");
static_assert(sizeof(HeartbeatMsg) == 8, "HeartbeatMsg");
static_assert(sizeof(RequestMsg) == 6, "RequestMsg");
static_assert(sizeof(SleepMsg) == 5, "SleepMsg");
static_assert(sizeof(TelemetryMsg) <= kEspNowMaxPayload, "ESP-NOW méretkorlát");

}  // namespace og
