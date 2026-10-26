#include "mini_test.h"
#include "pid_table.h"

using namespace og;

static void decode_known() {
    float v;
    const uint8_t rpm[] = {0x1A, 0xF8};   // (256*26+248)/4 = 1726
    CHECK(pid_decode(0x0C, rpm, 2, v)); CHECK_NEAR(v, 1726.0, 1e-3);
    const uint8_t spd[] = {0x78};
    CHECK(pid_decode(0x0D, spd, 1, v)); CHECK_NEAR(v, 120, 1e-3);
    const uint8_t clt[] = {0x7B};         // 123 - 40 = 83 °C
    CHECK(pid_decode(0x05, clt, 1, v)); CHECK_NEAR(v, 83, 1e-3);
    const uint8_t thr[] = {0xFF};
    CHECK(pid_decode(0x11, thr, 1, v)); CHECK_NEAR(v, 100, 1e-3);
    const uint8_t vb[] = {0x37, 0xE8};    // 14312 mV
    CHECK(pid_decode(0x42, vb, 2, v)); CHECK_NEAR(v, 14.312, 1e-4);
}

static void decode_rejects() {
    float v;
    const uint8_t one[] = {0x1A};
    CHECK(!pid_decode(0x0C, one, 1, v));   // kevés bájt
    CHECK(!pid_decode(0x99, one, 1, v));   // ismeretlen PID
    CHECK(!pid_decode(0x0C, nullptr, 2, v));
    CHECK(pid_find(0x0C) != nullptr && pid_find(0x99) == nullptr);
}

static void encode_decode_roundtrip() {
    struct { uint8_t pid; float val; double tol; } c[] = {
        {0x0C, 800.f, 0.25}, {0x0C, 6500.25f, 0.25}, {0x0D, 50.f, 0.5},
        {0x05, -10.f, 0.5}, {0x05, 95.f, 0.5}, {0x11, 33.f, 0.3}, {0x42, 12.6f, 0.001}};
    for (auto& t : c) {
        uint8_t b[2];
        size_t n = pid_encode(t.pid, t.val, b, sizeof b);
        float back = 0.f;
        CHECK(n > 0 && pid_decode(t.pid, b, n, back));
        CHECK_NEAR(back, t.val, t.tol);
    }
    uint8_t b[1];
    CHECK(pid_encode(0x0C, 1000.f, b, 1) == 0);   // kicsi puffer
    CHECK(pid_encode(0x99, 1.f, b, 1) == 0);
}

static void clamping() {
    uint8_t b[2];
    pid_encode(0x0D, 400.f, b, 2); CHECK(b[0] == 255);
    pid_encode(0x05, -100.f, b, 2); CHECK(b[0] == 0);
}

static void mil() {
    MilStatus m = decode_mil_status(0x83);
    CHECK(m.mil_on && m.dtc_count == 3);
    m = decode_mil_status(0x00);
    CHECK(!m.mil_on && m.dtc_count == 0);
}

static void bitmap() {
    const uint32_t bm = 0xBE1FA813;   // gyakori valós 01 00 válasz: BE 1F A8 13
    const uint8_t yes[] = {0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0C, 0x0D, 0x0E, 0x0F,
                           0x10, 0x11, 0x13, 0x15, 0x1C, 0x1F, 0x20};
    int cnt = 0;
    for (uint8_t p = 0x01; p <= 0x20; ++p) {
        bool expect = false;
        for (uint8_t y : yes) expect |= (y == p);
        CHECK(pid_in_bitmap(bm, 0x00, p) == expect);
        cnt += expect;
    }
    CHECK(cnt == 17);
    CHECK(!pid_in_bitmap(bm, 0x00, 0x00) && !pid_in_bitmap(bm, 0x00, 0x21));
    CHECK(pid_in_bitmap(0x80000000u, 0x20, 0x21));
}

int main() {
    RUN(decode_known);
    RUN(decode_rejects);
    RUN(encode_decode_roundtrip);
    RUN(clamping);
    RUN(mil);
    RUN(bitmap);
    TEST_MAIN_END();
}
