// OBD-kliens, ütemező és protokoll-felismerés tesztjei virtuális buszon.
#include <cstring>
#include <initializer_list>

#include "ecu_model.h"
#include "isotp.h"
#include "mini_test.h"
#include "obd_client.h"
#include "obd_detect.h"
#include "obd_services.h"
#include "pid_scheduler.h"
#include "pid_table.h"
#include "virtual_bus.h"

using namespace og;

static EcuConfig ecu_cfg(bool ext = false, uint8_t idx = 0, uint32_t lat = 40, EcuProtocol p = EcuProtocol::Classic) {
    EcuConfig c;
    c.ext_ids = ext;
    c.index = idx;
    c.latency_ms = lat;
    c.protocol = p;
    return c;
}

// ---------------------------------------------------------------- szolgáltatás-értelmezők
static void test_services() {
    uint8_t b[4];
    CHECK(obd_build_mode01(0x0C, b) == 2 && b[0] == 0x01 && b[1] == 0x0C);
    CHECK(obd_build_uds_read_did(0xF810, b) == 3 && b[0] == 0x22 && b[1] == 0xF8 && b[2] == 0x10);

    uint8_t sid = 0, nrc = 0;
    const uint8_t neg[3] = {0x7F, 0x01, 0x78};
    CHECK(obd_is_negative(neg, 3, sid, nrc) && sid == 0x01 && nrc == 0x78);
    const uint8_t pos[3] = {0x41, 0x0C, 0x00};
    CHECK(!obd_is_negative(pos, 3, sid, nrc));

    const uint8_t* d = nullptr;
    size_t dl = 0;
    CHECK(obd_parse_mode01(pos, 3, 0x0C, &d, dl) == Err::Ok && dl == 1);
    CHECK(obd_parse_mode01(pos, 3, 0x0D, &d, dl) == Err::ProtocolError);  // más PID
    CHECK(obd_parse_mode01(pos, 2, 0x0C, &d, dl) == Err::BadLength);

    char vin[18];
    uint8_t good[20] = {0x49, 0x02, 0x01};
    std::memcpy(&good[3], "WTEST12345678901A", 17);
    CHECK(obd_parse_vin(good, 20, vin) == Err::Ok && std::strcmp(vin, "WTEST12345678901A") == 0);
    CHECK(obd_parse_vin(good, 19, vin) == Err::BadLength);
    good[5] = 'I';  // az I nem szerepelhet VIN-ben
    CHECK(obd_parse_vin(good, 20, vin) == Err::ProtocolError);
    good[5] = 'e';  // kisbetű sem
    CHECK(obd_parse_vin(good, 20, vin) == Err::ProtocolError);

    const uint8_t sup[6] = {0x41, 0x00, 0xBE, 0x1F, 0xA8, 0x13};
    uint32_t bm = 0;
    CHECK(obd_parse_supported(sup, 6, 0x00, bm) == Err::Ok && bm == 0xBE1FA813u);
    CHECK(pid_in_bitmap(bm, 0x00, 0x0C) && !pid_in_bitmap(bm, 0x00, 0x02));
}

static void test_ecu_bitmap() {
    // A 00 blokk: 01, 05, 0C, 0D, 11 + a 20-as blokk jelzése; a 20-as blokk a 40-esét jelzi.
    const uint32_t b0 = EcuModel::supported_bitmap(0x00);
    for (uint8_t p : std::initializer_list<uint8_t>{0x01, 0x05, 0x0C, 0x0D, 0x11, 0x20}) CHECK(pid_in_bitmap(b0, 0x00, p));
    CHECK(!pid_in_bitmap(b0, 0x00, 0x02));
    CHECK(pid_in_bitmap(EcuModel::supported_bitmap(0x20), 0x20, 0x40));
    CHECK(pid_in_bitmap(EcuModel::supported_bitmap(0x40), 0x40, 0x42));
    CHECK((EcuModel::supported_bitmap(0x40) & 1u) == 0);  // nincs további blokk
}

// ---------------------------------------------------------------- kliens
static void test_client_pids() {
    VirtualBus bus;
    EcuModel ecu(ecu_cfg());
    bus.attach(&ecu);
    IsoTp tp(bus);
    ObdClient cl(tp);
    ecu.state.rpm = 3000;
    ecu.state.speed_kmh = 88;
    ecu.state.coolant_c = 75;
    ecu.state.throttle_pct = 40;
    ecu.state.vbat_v = 12.6f;
    struct { uint8_t pid; float expect; float eps; } cases[] = {
        {0x0C, 3000, 0.25f}, {0x0D, 88, 0.5f}, {0x05, 75, 0.5f}, {0x11, 40, 0.5f}, {0x42, 12.6f, 0.002f}};
    for (const auto& c : cases) {
        uint8_t d[8];
        size_t n = 0;
        float v = 0;
        CHECK(cl.read_pid(c.pid, d, sizeof d, n) == Err::Ok);
        CHECK(pid_decode(c.pid, d, n, v));
        CHECK_NEAR(v, c.expect, c.eps);
    }
}

static void test_client_unsupported_and_negative() {
    VirtualBus bus;
    EcuModel ecu(ecu_cfg());
    bus.attach(&ecu);
    IsoTp tp(bus);
    ObdClient cl(tp);
    uint8_t d[8];
    size_t n = 0;
    CHECK(cl.read_pid(0x77, d, sizeof d, n) == Err::Timeout);  // nem támogatott PID: nincs válasz

    // Fizikai címzésre ismeretlen szolgáltatás -> 7F 04 11.
    cl.set_target(0x7E0);
    const uint8_t req[1] = {0x04};
    uint8_t resp[8];
    CHECK(cl.request(req, 1, resp, sizeof resp, n) == Err::NegativeResponse);
    CHECK(cl.last_nrc() == 0x11);
    // Funkcionális címzésre az ECU hallgat.
    cl.set_target(0);
    CHECK(cl.request(req, 1, resp, sizeof resp, n) == Err::Timeout);
}

static void test_client_vin_dtc_supported() {
    VirtualBus bus;
    EcuModel ecu(ecu_cfg());
    bus.attach(&ecu);
    IsoTp tp(bus);
    ObdClient cl(tp);
    char vin[18];
    CHECK(cl.read_vin(vin) == Err::Ok);
    CHECK(std::strcmp(vin, "WTEST12345678901A") == 0);

    ecu.state.dtc_count = 3;
    uint8_t cnt = 0;
    CHECK(cl.read_dtc_count(cnt) == Err::Ok && cnt == 3);

    uint32_t bm = 0;
    CHECK(cl.read_supported(0x00, bm) == Err::Ok);
    CHECK(bm == EcuModel::supported_bitmap(0x00));
}

static void test_client_response_pending_and_stale() {
    VirtualBus bus;
    EcuModel ecu(ecu_cfg(false, 0, 40));
    bus.attach(&ecu);
    IsoTp tp(bus);
    ObdClient cl(tp);

    // Késve érkező, másik PID-hez tartozó válasz (a kérés után 5 ms-mal): el kell dobni.
    CanFrame stale;
    stale.id = 0x7E8;
    stale.dlc = 8;
    const uint8_t s[8] = {0x03, 0x41, 0x0D, 0x10, 0, 0, 0, 0};
    std::memcpy(stale.data, s, 8);
    bus.inject(stale, 5);
    uint8_t d[8];
    size_t n = 0;
    CHECK(cl.read_pid(0x0C, d, sizeof d, n) == Err::Ok);
    CHECK(n == 2);  // a 0C két adatbájtja, nem a 0D egy bájtja

    // "Response pending": az ECU előbb 7F 01 78-at küld, a valódi választ 1 s-mal később.
    CanFrame pend;
    pend.id = 0x7E8;
    pend.dlc = 8;
    const uint8_t p[8] = {0x03, 0x7F, 0x01, 0x78, 0, 0, 0, 0};
    std::memcpy(pend.data, p, 8);
    CanFrame late;
    late.id = 0x7E8;
    late.dlc = 8;
    const uint8_t l[8] = {0x03, 0x41, 0x0D, 0x2A, 0, 0, 0, 0};
    std::memcpy(late.data, l, 8);
    VirtualBus bus2;  // ECU nélkül: kézzel adjuk a két keretet
    IsoTp tp2(bus2);
    ObdClient cl2(tp2);
    bus2.inject(pend, 20);
    bus2.inject(late, 1000);
    CHECK(cl2.read_pid(0x0D, d, sizeof d, n) == Err::Ok);
    CHECK(n == 1 && d[0] == 0x2A);
    CHECK(bus2.now_ms() >= 1000);  // P2* (5 s) határidővel várt, nem P2-vel
}

// ---------------------------------------------------------------- ütemező
static void add_plan(PidScheduler& s, uint32_t now) {
    s.add(0x0C, 100, now);    // fordulatszám 10 Hz
    s.add(0x0D, 200, now);    // sebesség 5 Hz
    s.add(0x11, 333, now);    // fojtószelep 3 Hz
    s.add(0x05, 1000, now);   // hűtővíz 1 Hz
    s.add(0x42, 2000, now);   // akkufeszültség 0,5 Hz
    s.add(0x01, 5000, now);   // MIL/DTC 0,2 Hz
}

static void run_for(PidScheduler& s, VirtualBus& bus, ObdClient& cl, uint32_t ms) {
    const uint32_t end = bus.now_ms() + ms;
    while (static_cast<int32_t>(end - bus.now_ms()) > 0) {
        Err e;
        if (!s.step(cl, e)) {
            // Nincs esedékes PID: alszunk a következőig (virtuális órán).
            CanFrame f;
            uint32_t w = s.ms_until_next(bus.now_ms());
            bus.recv(f, w ? w : 1);
        }
    }
}

static void test_scheduler_nominal_load() {
    VirtualBus bus;
    EcuModel ecu(ecu_cfg(false, 0, 40));
    bus.attach(&ecu);
    IsoTp tp(bus);
    ObdClient cl(tp);
    ecu.state.rpm = 3000;
    ecu.state.mil_on = true;
    ecu.state.dtc_count = 2;
    PidScheduler s;
    add_plan(s, bus.now_ms());
    run_for(s, bus, cl, 10000);

    // 10 s alatt a tervezett lekérdezésszám: 100 + 50 + 30 + 10 + 5 + 2 = ~197.
    CHECK(s.requests(0x0C) >= 98 && s.requests(0x0C) <= 102);
    CHECK(s.requests(0x0D) >= 48 && s.requests(0x0D) <= 52);
    CHECK(s.requests(0x11) >= 29 && s.requests(0x11) <= 32);
    CHECK(s.requests(0x05) >= 9 && s.requests(0x05) <= 11);
    uint32_t total = 0;
    for (uint8_t p : std::initializer_list<uint8_t>{0x0C, 0x0D, 0x11, 0x05, 0x42, 0x01}) {
        total += s.requests(p);
        CHECK(s.failures(p) == 0);
    }
    CHECK(total >= 190 && total <= 210);
    // Az ECU-n eltöltött idő a 10 s kb. 79 %-a (a szűk keresztmetszet az ECU válaszideje).
    const double load = total * 40.0 / 10000.0;
    CHECK(load > 0.75 && load < 0.85);

    float v = 0;
    uint32_t age = 0;
    CHECK(s.value(0x0C, v, bus.now_ms(), age));
    CHECK_NEAR(v, 3000, 0.25);
    CHECK(age < 200);  // 10 Hz-es PID frissessége
    MilStatus m{};
    CHECK(s.mil(m) && m.mil_on && m.dtc_count == 2);
}

static void test_scheduler_overload() {
    // Lassú ECU (150 ms/kérés): a terv nem tartható, de senki nem éhezhet ki,
    // és a gyors PID több lekérdezést kap a lassúnál.
    VirtualBus bus;
    EcuModel ecu(ecu_cfg(false, 0, 150));
    bus.attach(&ecu);
    IsoTp tp(bus);
    ObdClientConfig cc;
    cc.p2_ms = 200;
    ObdClient cl(tp, cc);
    PidScheduler s;
    add_plan(s, bus.now_ms());
    run_for(s, bus, cl, 10000);
    for (uint8_t p : std::initializer_list<uint8_t>{0x0C, 0x0D, 0x11, 0x05, 0x42, 0x01}) CHECK(s.requests(p) >= 1);
    CHECK(s.requests(0x0C) > s.requests(0x05));
    CHECK(s.requests(0x0C) > s.requests(0x0D));
    uint32_t total = 0;
    for (uint8_t p : std::initializer_list<uint8_t>{0x0C, 0x0D, 0x11, 0x05, 0x42, 0x01}) total += s.requests(p);
    CHECK(total <= 10000 / 150 + 1);  // az ECU nem tud gyorsabban válaszolni
}

static void test_scheduler_failure_recovery() {
    VirtualBus bus;
    EcuModel ecu(ecu_cfg());
    bus.attach(&ecu);
    IsoTp tp(bus);
    ObdClient cl(tp);
    PidScheduler s;
    s.add(0x0C, 100, bus.now_ms());
    run_for(s, bus, cl, 500);
    CHECK(s.failures(0x0C) == 0);

    bus.set_bus_bitrate(250000);  // a busz "eltűnik" (nincs nyugtázás)
    run_for(s, bus, cl, 1000);
    CHECK(s.consecutive_failures(0x0C) >= 3);
    float v;
    uint32_t age = 0;
    CHECK(s.value(0x0C, v, bus.now_ms(), age) && age >= 1000);  // az érték elavult: a megjelenítő ebből látja

    bus.set_bus_bitrate(500000);  // visszatér
    run_for(s, bus, cl, 500);
    CHECK(s.consecutive_failures(0x0C) == 0);
    CHECK(s.value(0x0C, v, bus.now_ms(), age) && age < 200);
}

static void test_scheduler_misc() {
    PidScheduler s;
    CHECK(!s.add(0x0C, 0, 0));          // 0 periódus nem megengedett
    CHECK(s.add(0x0C, 100, 0));
    CHECK(!s.add(0x0C, 100, 0));        // kétszeres felvétel nem megengedett
    uint8_t pid = 0;
    CHECK(s.next_due(0, pid) && pid == 0x0C);
    CHECK(s.ms_until_next(0) == 0);
    PidScheduler empty;
    CHECK(!empty.next_due(0, pid));
    CHECK(empty.ms_until_next(0) == 0xFFFFFFFFu);
    // Órát átfordulása: esedékesség 0xFFFFFFF0-n, "most" 0x10 -> 32 ms késés.
    PidScheduler w;
    w.add(0x0C, 100, 0xFFFFFFF0u);
    CHECK(w.next_due(0x10, pid));
}

// ---------------------------------------------------------------- protokoll-felismerés
static void detect_classic(uint32_t bitrate, bool ext) {
    VirtualBus bus(bitrate);
    EcuModel ecu(ecu_cfg(ext, 0));
    bus.attach(&ecu);
    ProtocolInfo info;
    CHECK(obd_detect(bus, DetectConfig(), info) == Err::Ok);
    CHECK(info.kind == ProtocolKind::Classic);
    CHECK(info.bitrate_bps == bitrate);
    CHECK(info.ext_ids == ext);
    CHECK(info.ecu_count == 1);
    CHECK(info.ecu_response_id[0] == ecu.response_id());
    CHECK(info.primary_request_id == ecu.request_id());
    CHECK(info.has_supported[0] && info.has_supported[1] && info.has_supported[2]);
    CHECK(info.supported[0] == EcuModel::supported_bitmap(0x00));
    CHECK(pid_in_bitmap(info.supported[0], 0x00, 0x0C));
    CHECK(pid_in_bitmap(info.supported[2], 0x40, 0x42));

    // A felismerés után a port a megtalált beállításon marad: a kliens azonnal használható.
    IsoTpConfig tc;
    tc.ext_ids = ext;
    IsoTp tp(bus, tc);
    ObdClient cl(tp);
    cl.set_target(info.primary_request_id);
    uint8_t d[8];
    size_t n = 0;
    CHECK(cl.read_pid(0x0C, d, sizeof d, n) == Err::Ok);
}

static void test_detect_classic_all_variants() {
    detect_classic(500000, false);
    detect_classic(250000, false);
    detect_classic(500000, true);
    detect_classic(250000, true);
}

static void test_detect_multiple_ecus() {
    VirtualBus bus;
    EcuModel engine(ecu_cfg(false, 1));  // 0x7E9
    EcuModel trans(ecu_cfg(false, 0));   // 0x7E8
    bus.attach(&engine);
    bus.attach(&trans);
    ProtocolInfo info;
    CHECK(obd_detect(bus, DetectConfig(), info) == Err::Ok);
    CHECK(info.ecu_count == 2);
    CHECK(info.primary_request_id == 0x7E0);  // a legkisebb azonosítójú ECU
}

static void test_detect_obd_on_uds() {
    VirtualBus bus(250000);
    EcuModel ecu(ecu_cfg(true, 0, 40, EcuProtocol::UdsOnly));
    bus.attach(&ecu);
    ProtocolInfo info;
    CHECK(obd_detect(bus, DetectConfig(), info) == Err::NotSupported);  // felismerve, de nem támogatott
    CHECK(info.kind == ProtocolKind::ObdOnUds);
    CHECK(info.bitrate_bps == 250000 && info.ext_ids);
    CHECK(info.ecu_count == 1);
}

static void test_detect_nothing() {
    VirtualBus bus;  // nincs ECU a buszon
    ProtocolInfo info;
    CHECK(obd_detect(bus, DetectConfig(), info) == Err::Timeout);
    CHECK(info.kind == ProtocolKind::None);
}

int main() {
    RUN(test_services);
    RUN(test_ecu_bitmap);
    RUN(test_client_pids);
    RUN(test_client_unsupported_and_negative);
    RUN(test_client_vin_dtc_supported);
    RUN(test_client_response_pending_and_stale);
    RUN(test_scheduler_nominal_load);
    RUN(test_scheduler_overload);
    RUN(test_scheduler_failure_recovery);
    RUN(test_scheduler_misc);
    RUN(test_detect_classic_all_variants);
    RUN(test_detect_multiple_ecus);
    RUN(test_detect_obd_on_uds);
    RUN(test_detect_nothing);
    TEST_MAIN_END();
}
