// A kijelző adatmodelljének tesztjei (frissesség, érvényesség, simítás, kapcsolatstatisztika).
#include <cstring>

#include "dash_model.h"
#include "mini_test.h"
#include "og_codec.h"

using namespace og;

static TelemetryMsg telemetry(uint16_t seq, uint16_t flags, float rpm) {
    TelemetryMsg m = {};
    m.h = make_header(MsgType::Telemetry, seq);
    m.flags = flags;
    m.rpm_q4 = static_cast<uint16_t>(rpm * 4.f);
    m.speed_kmh = 50;
    m.coolant_c = 88;
    m.throttle_raw = 255;
    m.vbat_mv = 14100;
    m.mil_dtc = 0x80 | 2;
    return m;
}

static bool feed(DashModel& d, const TelemetryMsg& m, uint32_t t, int8_t rssi = -40) {
    uint8_t buf[sizeof(TelemetryMsg)];
    std::memcpy(buf, &m, sizeof m);
    return d.on_packet(buf, sizeof buf, rssi, t);
}

static void no_data_at_start() {
    DashModel d;
    const DashValues v = d.values(1000);
    CHECK(!v.has_data && !v.fresh);
}

static void values_and_freshness() {
    DashModel d;
    const uint16_t all = kFlagRpm | kFlagSpeed | kFlagCoolant | kFlagThrottle | kFlagVbat | kFlagMil;
    CHECK(feed(d, telemetry(1, all, 3000.f), 1000));
    DashValues v = d.values(1000);
    CHECK(v.has_data && v.fresh && v.flags == all);
    CHECK_NEAR(v.rpm, 3000, 1e-3);           // az első minta nem ugrik nulláról
    CHECK_NEAR(v.coolant_c, 88, 1e-3);
    CHECK_NEAR(v.throttle_pct, 100, 1e-3);
    CHECK_NEAR(v.vbat_v, 14.1, 1e-3);
    CHECK(v.mil_on && v.dtc_count == 2);

    CHECK(feed(d, telemetry(2, all, 4000.f), 1100));
    CHECK_NEAR(d.values(1150).rpm, 3500, 1e-3);  // simítva félúton
    CHECK(d.values(1100 + DashModel::kStaleMs).fresh);
    CHECK(!d.values(1100 + DashModel::kStaleMs + 1).fresh);  // elavult: a kijelző "nincs adatot" mutat
}

static void invalid_and_duplicate() {
    DashModel d;
    uint8_t junk[3] = {1, 2, 3};
    CHECK(!d.on_packet(junk, sizeof junk, -50, 0));
    CHECK(feed(d, telemetry(10, kFlagRpm, 1000.f), 0));
    CHECK(!feed(d, telemetry(10, kFlagRpm, 9999.f), 10));  // duplikátum: nem írja felül
    CHECK_NEAR(d.values(500).rpm, 1000, 1e-3);
    CHECK(feed(d, telemetry(13, kFlagRpm, 1000.f), 100));
    const DashLink l = d.link();
    CHECK(l.invalid == 1 && l.received == 2 && l.lost == 2 && l.stale == 1 && l.last_seq == 13);
}

static void per_second_and_rssi() {
    DashModel d;
    feed(d, telemetry(1, 0, 0.f), 0, -30);
    feed(d, telemetry(2, 0, 0.f), 100, -40);
    d.on_second();
    DashLink l = d.link();
    CHECK(l.per_sec == 2 && l.rssi_dbm == -35);
    d.on_second();  // csomag nélküli másodperc
    l = d.link();
    CHECK(l.per_sec == 0 && l.rssi_dbm == 0);
}

int main() {
    RUN(no_data_at_start);
    RUN(values_and_freshness);
    RUN(invalid_and_duplicate);
    RUN(per_second_and_rssi);
    TEST_MAIN_END();
}
