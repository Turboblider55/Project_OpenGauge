// ISO-TP réteg tesztjei: virtuális buszon, virtuális ECU-val.
#include <cstring>

#include "ecu_model.h"
#include "isotp.h"
#include "mini_test.h"
#include "virtual_bus.h"

using namespace og;

namespace {
const uint8_t kReqRpm[2] = {0x01, 0x0C};
const uint8_t kReqVin[2] = {0x09, 0x02};

// Egyszerű helper: ECU a buszon, alapbeállítással.
struct Rig {
    VirtualBus bus;
    EcuModel ecu;
    IsoTp tp;
    explicit Rig(bool ext = false, uint32_t start = 0, uint8_t bs = 0, uint8_t ecu_stmin = 0, uint32_t lat = 40)
        : bus(500000, start), ecu(make(ext, ecu_stmin, lat)), tp(bus, make_tp(ext, bs)) {
        bus.attach(&ecu);
    }
    static EcuConfig make(bool ext, uint8_t stmin, uint32_t lat) {
        EcuConfig c;
        c.ext_ids = ext;
        c.st_min_ms = stmin;
        c.latency_ms = lat;
        return c;
    }
    static IsoTpConfig make_tp(bool ext, uint8_t bs) {
        IsoTpConfig c;
        c.ext_ids = ext;
        c.block_size = bs;
        return c;
    }
};
}  // namespace

static void test_id_helpers() {
    CHECK(IsoTp::is_response_id(0x7E8, false));
    CHECK(IsoTp::is_response_id(0x7EF, false));
    CHECK(!IsoTp::is_response_id(0x7E7, false));
    CHECK(!IsoTp::is_response_id(0x7DF, false));
    CHECK(IsoTp::is_response_id(0x18DAF1E8, true));
    CHECK(!IsoTp::is_response_id(0x18DAE8F1, true));
    CHECK(IsoTp::request_id_for(0x7E8, false) == 0x7E0);
    CHECK(IsoTp::request_id_for(0x7EA, false) == 0x7E2);
    CHECK(IsoTp::request_id_for(0x18DAF1E8, true) == 0x18DAE8F1);
}

static void test_single_frame() {
    Rig r;
    CHECK(r.tp.send_functional(kReqRpm, 2) == Err::Ok);
    uint8_t buf[16];
    size_t n = 0;
    uint32_t src = 0;
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::Ok);
    CHECK(n == 4);
    CHECK(src == 0x7E8);
    CHECK(buf[0] == 0x41 && buf[1] == 0x0C);
    // 850 /perc -> 3400 = 0x0D48
    CHECK(buf[2] == 0x0D && buf[3] == 0x48);
    // A kérés 8 bájtos, kitöltött keret volt.
    CHECK(r.bus.tx_log().size() == 1);
    CHECK(r.bus.tx_log()[0].id == 0x7DF && r.bus.tx_log()[0].dlc == 8);
    CHECK(r.bus.tx_log()[0].data[0] == 0x02 && r.bus.tx_log()[0].data[7] == 0x00);
    // A válaszidő: 40 ms a virtuális órán.
    CHECK(r.bus.now_ms() == 40);
}

static void test_multiframe_vin() {
    Rig r;
    CHECK(r.tp.send_physical(0x7E0, kReqVin, 2) == Err::Ok);
    uint8_t buf[64];
    size_t n = 0;
    uint32_t src = 0;
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::Ok);
    CHECK(n == 20);
    CHECK(buf[0] == 0x49 && buf[1] == 0x02 && buf[2] == 0x01);
    CHECK(std::memcmp(&buf[3], "WTEST12345678901A", 17) == 0);
    // A teszter egy FC keretet küldött az ECU fizikai címére: 30 00 00.
    int fc = 0;
    for (const CanFrame& f : r.bus.tx_log())
        if ((f.data[0] >> 4) == 3) {
            ++fc;
            CHECK(f.id == 0x7E0 && f.data[0] == 0x30 && f.data[1] == 0 && f.data[2] == 0);
        }
    CHECK(fc == 1);
}

static void test_extended_ids() {
    Rig r(true);
    CHECK(r.tp.functional_id() == 0x18DB33F1);
    CHECK(r.tp.send_functional(kReqVin, 2) == Err::Ok);
    uint8_t buf[64];
    size_t n = 0;
    uint32_t src = 0;
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::Ok);
    CHECK(n == 20);
    CHECK(src == 0x18DAF1E8);
    // az FC a 29 bites fizikai címre ment
    bool fc_ok = false;
    for (const CanFrame& f : r.bus.tx_log())
        if ((f.data[0] >> 4) == 3) fc_ok = f.ext && f.id == 0x18DAE8F1;
    CHECK(fc_ok);
}

static void test_timeout() {
    VirtualBus bus;  // nincs ECU
    IsoTp tp(bus);
    CHECK(tp.send_functional(kReqRpm, 2) == Err::Ok);
    uint8_t buf[8];
    size_t n = 0;
    uint32_t src = 0;
    const uint32_t t0 = bus.now_ms();
    CHECK(tp.receive(buf, sizeof buf, n, src, 50) == Err::Timeout);
    CHECK(bus.now_ms() - t0 == 50);  // pontosan a határidőig várt
}

static void test_missing_consecutive_frame() {
    Rig r;
    int cf_seen = 0;
    // Az első folytatókeretet eldobjuk -> a másodiknak 2-es sorszáma van, 1-et várunk.
    r.bus.rx_hook = [&](CanFrame& f, bool& drop) {
        if ((f.data[0] >> 4) == 2 && ++cf_seen == 1) drop = true;
    };
    r.tp.send_physical(0x7E0, kReqVin, 2);
    uint8_t buf[64];
    size_t n = 0;
    uint32_t src = 0;
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::SequenceError);
}

static void test_lost_consecutive_timeout() {
    Rig r;
    int cf_seen = 0;
    // Az utolsó CF-et elveszítjük -> n_cr (150 ms) után időtúllépés.
    r.bus.rx_hook = [&](CanFrame& f, bool& drop) {
        if ((f.data[0] >> 4) == 2 && ++cf_seen == 2) drop = true;
    };
    r.tp.send_physical(0x7E0, kReqVin, 2);
    uint8_t buf[64];
    size_t n = 0;
    uint32_t src = 0;
    const uint32_t t0 = r.bus.now_ms();
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::Timeout);
    CHECK(r.bus.now_ms() - t0 >= 150);  // n_cr_ms után adta fel
}

static void test_buffer_too_small() {
    Rig r;
    r.tp.send_physical(0x7E0, kReqVin, 2);
    uint8_t buf[10];
    size_t n = 0;
    uint32_t src = 0;
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::BufferTooSmall);
    // Az ECU-nak Overflow FC-t (0x32) kellett küldeni.
    bool overflow = false;
    for (const CanFrame& f : r.bus.tx_log())
        if (f.data[0] == 0x32) overflow = true;
    CHECK(overflow);
}

static void test_foreign_traffic_ignored() {
    Rig r;
    CanFrame noise;
    noise.id = 0x123;  // nem OBD-azonosító
    noise.dlc = 8;
    noise.data[0] = 0x02;
    r.bus.inject(noise, 5);
    CanFrame stray;  // elkóborolt folytatókeret egy OBD-azonosítóról
    stray.id = 0x7E9;
    stray.dlc = 8;
    stray.data[0] = 0x25;
    r.bus.inject(stray, 10);
    r.tp.send_functional(kReqRpm, 2);
    uint8_t buf[16];
    size_t n = 0;
    uint32_t src = 0;
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::Ok);
    CHECK(n == 4 && src == 0x7E8);
}

static void test_clock_wraparound() {
    // A virtuális óra a 32 bites átforduláshoz közel indul: a határidők így is helyesek.
    Rig r(false, 0xFFFFFFE0u);
    r.tp.send_physical(0x7E0, kReqVin, 2);
    uint8_t buf[64];
    size_t n = 0;
    uint32_t src = 0;
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::Ok);
    CHECK(n == 20);
    CHECK(r.bus.now_ms() < 0x100);  // átfordult
}

static void test_block_size_and_stmin() {
    // BS=1: minden folytatókeret után új FC kell; az ECU 5 ms keretközt tart.
    Rig r(false, 0, 1, 5);
    r.tp.send_physical(0x7E0, kReqVin, 2);
    uint8_t buf[64];
    size_t n = 0;
    uint32_t src = 0;
    CHECK(r.tp.receive(buf, sizeof buf, n, src, 100) == Err::Ok);
    CHECK(n == 20 && std::memcmp(&buf[3], "WTEST12345678901A", 17) == 0);
    int fc = 0;
    for (const CanFrame& f : r.bus.tx_log())
        if ((f.data[0] >> 4) == 3) ++fc;
    CHECK(fc == 2);  // egy az FF után, egy az első CF után
}

static void test_send_arguments() {
    VirtualBus bus;
    IsoTp tp(bus);
    uint8_t big[8] = {0};
    CHECK(tp.send_functional(big, 8) == Err::InvalidArg);   // legfeljebb 7 bájt
    CHECK(tp.send_functional(big, 0) == Err::InvalidArg);
    CHECK(tp.send_functional(nullptr, 2) == Err::InvalidArg);
    bus.set_bus_bitrate(250000);  // más sebesség: nincs nyugtázás
    CHECK(tp.send_functional(kReqRpm, 2) == Err::BusError);
}

int main() {
    RUN(test_id_helpers);
    RUN(test_single_frame);
    RUN(test_multiframe_vin);
    RUN(test_extended_ids);
    RUN(test_timeout);
    RUN(test_missing_consecutive_frame);
    RUN(test_lost_consecutive_timeout);
    RUN(test_buffer_too_small);
    RUN(test_foreign_traffic_ignored);
    RUN(test_clock_wraparound);
    RUN(test_block_size_and_stmin);
    RUN(test_send_arguments);
    TEST_MAIN_END();
}
