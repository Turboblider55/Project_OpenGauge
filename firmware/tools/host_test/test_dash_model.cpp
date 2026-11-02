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

// Az életjel a kapcsolatot élőnek jelzi, de nem telemetria: nem friss adat, nem is érvénytelen csomag.
static void heartbeat_keeps_peer_alive() {
    DashModel d;
    CHECK(!d.peer_alive(0));
    HeartbeatMsg hb = {};
    hb.h = make_header(MsgType::Heartbeat, 1);
    uint8_t buf[sizeof(HeartbeatMsg)];
    std::memcpy(buf, &hb, sizeof hb);
    CHECK(!d.on_packet(buf, sizeof buf, -40, 1000));
    CHECK(d.peer_alive(1000 + kPeerTimeoutMs));
    CHECK(!d.peer_alive(1001 + kPeerTimeoutMs));
    CHECK(!d.values(1000).has_data);
    CHECK(d.link().invalid == 0);
    CHECK(d.link().received == 0);
    feed(d, telemetry(1, kFlagRpm, 1000.f), 5000);
    CHECK(d.peer_alive(5000));
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

static void rolling_loss_window() {
    DashModel d;
    uint16_t seq = 0;
    // 60 s hibátlan adás, majd 60 s, amelyben minden 10. csomag elvész.
    for (int s = 0; s < 60; ++s) {
        for (int i = 0; i < 10; ++i) feed(d, telemetry(seq++, 0, 0.f), s * 1000 + i * 100);
        d.on_second();
    }
    CHECK_NEAR(d.link().loss_ratio_window, 0.f, 1e-6);
    for (int s = 60; s < 120; ++s) {
        for (int i = 0; i < 10; ++i, ++seq)
            if (i != 5) feed(d, telemetry(seq, 0, 0.f), s * 1000 + i * 100);
        d.on_second();
    }
    CHECK_NEAR(d.link().loss_ratio_window, 0.1, 1e-3);  // az ablak csak az utolsó 60 s-ot látja
    CHECK_NEAR(d.link().loss_ratio, 60.0 / 1200.0, 1e-3);  // a teljes futás átlaga a fele
    // Az adó újraindul (a sorszám 0-ra ugrik): a statisztika nullázódik, az ablak is újrakezdődik a következő másodperctől.
    // (a kis sorszámról 0-ra ugrás régebbi csomagnak látszik: kResyncAfter csomag után áll át)
    for (uint16_t q = 0; q < LinkStats::kResyncAfter; ++q) feed(d, telemetry(q, 0, 0.f), 120000 + q * 100);
    d.on_second();
    CHECK(d.link().resyncs == 1 && d.link().lost == 0 && d.link().received == 1);
    CHECK(d.link().loss_ratio_window < 0.11f);
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

static void engine_stopped_message_sets_and_new_data_clears() {
    DashModel d;
    SleepMsg sl = {};
    sl.h = make_header(MsgType::Sleep, 1);
    sl.reason = static_cast<uint8_t>(SleepReason::EngineStopped);
    uint8_t buf[sizeof(SleepMsg)];
    CHECK(encode(sl, buf, sizeof buf) == sizeof(SleepMsg));
    CHECK(!d.on_packet(buf, sizeof buf, -40, 5000));
    CHECK(d.engine_stopped());
    CHECK(d.stopped_ms() == 5000);
    CHECK(d.peer_alive(5100));
    TelemetryMsg t = {};
    t.h = make_header(MsgType::Telemetry, 1);
    t.flags = kFlagRpm;
    uint8_t tb[sizeof(TelemetryMsg)];
    CHECK(encode(t, tb, sizeof tb) == sizeof(TelemetryMsg));
    CHECK(d.on_packet(tb, sizeof tb, -40, 6000));
    CHECK(!d.engine_stopped());
}

static void sleep_now_message_is_not_engine_stop() {
    DashModel d;
    SleepMsg sl = {};
    sl.h = make_header(MsgType::Sleep, 1);
    sl.reason = static_cast<uint8_t>(SleepReason::Sleep);
    uint8_t buf[sizeof(SleepMsg)];
    CHECK(encode(sl, buf, sizeof buf) == sizeof(SleepMsg));
    d.on_packet(buf, sizeof buf, -40, 5000);
    CHECK(!d.engine_stopped());
}

int main() {
    RUN(no_data_at_start);
    RUN(heartbeat_keeps_peer_alive);
    RUN(engine_stopped_message_sets_and_new_data_clears);
    RUN(sleep_now_message_is_not_engine_stop);
    RUN(values_and_freshness);
    RUN(invalid_and_duplicate);
    RUN(rolling_loss_window);
    RUN(per_second_and_rssi);
    TEST_MAIN_END();
}
