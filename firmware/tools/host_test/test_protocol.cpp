#include <string>
#include "mini_test.h"
#include "og_codec.h"
#include "og_link_stats.h"
#include "og_seq.h"

using namespace og;

static void roundtrip_telemetry() {
    TelemetryMsg m{};
    m.h = make_header(MsgType::Telemetry, 1234);
    m.flags = kFlagRpm | kFlagSpeed;
    m.rpm_q4 = 3000 * 4;
    m.speed_kmh = 87;
    m.coolant_c = -12;
    m.throttle_raw = 128;
    m.vbat_mv = 14100;
    m.mil_dtc = 0x82;
    uint8_t buf[kEspNowMaxPayload];
    size_t n = encode(m, buf, sizeof buf);
    CHECK(n == 15);
    TelemetryMsg o{};
    CHECK(decode(buf, n, o));
    CHECK(o.h.seq == 1234 && o.rpm_q4 == 12000 && o.speed_kmh == 87);
    CHECK(o.coolant_c == -12 && o.vbat_mv == 14100 && o.mil_dtc == 0x82);
    CHECK(o.flags == (kFlagRpm | kFlagSpeed));
}

static void little_endian_layout() {
    TelemetryMsg m{};
    m.h = make_header(MsgType::Telemetry, 0x0102);
    uint8_t buf[16];
    encode(m, buf, sizeof buf);
    CHECK(buf[0] == kProtoVersion && buf[1] == 0x01 && buf[2] == 0x02 && buf[3] == 0x01);
}

static void rejects_bad_input() {
    TelemetryMsg m{};
    m.h = make_header(MsgType::Telemetry, 1);
    uint8_t buf[32];
    size_t n = encode(m, buf, sizeof buf);
    TelemetryMsg o{};
    CHECK(!decode(buf, n - 1, o));          // rövid
    CHECK(!decode(buf, n + 1, o));          // hosszú
    buf[0] = kProtoVersion + 1;
    CHECK(!decode(buf, n, o));              // ismeretlen verzió
    buf[0] = kProtoVersion;
    buf[1] = static_cast<uint8_t>(MsgType::Vin);
    CHECK(!decode(buf, n, o));              // más típus
    CHECK(encode(m, buf, 14) == 0);         // kicsi puffer
    Header h;
    CHECK(!decode_header(nullptr, 4, h) && !decode_header(buf, 3, h));
}

static void other_messages() {
    VinMsg v{};
    v.h = make_header(MsgType::Vin, 7);
    std::snprintf(v.vin, sizeof v.vin, "WVWZZZ1JZXW000001");
    uint8_t buf[64];
    size_t n = encode(v, buf, sizeof buf);
    VinMsg o{};
    CHECK(n == 22 && decode(buf, n, o) && std::string(o.vin) == "WVWZZZ1JZXW000001");
    RequestMsg r{make_header(MsgType::Request, 1), static_cast<uint8_t>(RequestCode::ReadDtc), 0};
    RequestMsg ro{};
    n = encode(r, buf, sizeof buf);
    CHECK(n == 6 && decode(buf, n, ro) && ro.code == 1);
}

static void sequence() {
    CHECK(seq_gap(10, 11) == 0);
    CHECK(seq_gap(10, 14) == 3);
    CHECK(seq_gap(65535, 0) == 0);      // átfordulás, nincs veszteség
    CHECK(seq_gap(65534, 1) == 2);
    CHECK(seq_is_newer(10, 11));
    CHECK(seq_is_newer(65535, 0));
    CHECK(!seq_is_newer(11, 11));       // duplikátum
    CHECK(!seq_is_newer(11, 10));       // késve érkezett
}

static void link_stats() {
    LinkStats st;
    CHECK(st.on_seq(100) && st.received() == 1 && st.lost() == 0);
    CHECK(st.on_seq(101) && st.lost() == 0);
    CHECK(st.on_seq(105) && st.lost() == 3);        // 102..104 elveszett
    CHECK(!st.on_seq(105) && st.stale() == 1);      // duplikátum
    CHECK(!st.on_seq(103) && st.stale() == 2);      // késve érkezett
    CHECK(st.on_seq(106) && st.last_seq() == 106);  // a sorozat folytatódik
    CHECK_NEAR(st.loss_ratio(), 3.0 / 7.0, 1e-6);   // 4 vett (100, 101, 105, 106) + 3 elveszett
    // Átfordulás veszteség nélkül.
    LinkStats w;
    w.on_seq(65535);
    CHECK(w.on_seq(0) && w.lost() == 0);
    // Az adó újraindul (sorszám 0-tól): kResyncAfter régebbi csomag után átáll.
    LinkStats r;
    r.on_seq(5000);
    CHECK(!r.on_seq(0));
    CHECK(!r.on_seq(1));
    CHECK(r.on_seq(2) && r.resyncs() == 1 && r.last_seq() == 2);
    CHECK(r.on_seq(3) && r.lost() == 0);
}

int main() {
    RUN(roundtrip_telemetry);
    RUN(little_endian_layout);
    RUN(rejects_bad_input);
    RUN(other_messages);
    RUN(sequence);
    RUN(link_stats);
    TEST_MAIN_END();
}
