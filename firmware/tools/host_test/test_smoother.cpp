#include "mini_test.h"
#include "smoother.h"

using namespace og;

static void first_sample_no_jump() {
    Smoother s(100);
    CHECK(s.value(0) == 0.f);                // még nincs adat
    s.push(3000.f, 1000);
    CHECK_NEAR(s.value(1000), 3000, 1e-3);   // nem ugrik nulláról
}

static void linear_transition() {
    Smoother s(100);
    s.push(1000.f, 0);
    s.push(2000.f, 100);
    CHECK_NEAR(s.value(100), 1000, 1e-3);
    CHECK_NEAR(s.value(150), 1500, 1e-3);
    CHECK_NEAR(s.value(200), 2000, 1e-3);
    CHECK_NEAR(s.value(500), 2000, 1e-3);    // utána megáll
}

static void new_sample_midway() {
    Smoother s(100);
    s.push(0.f, 0);
    s.push(100.f, 10);       // 10 ms-nál indul 0-ról
    s.push(200.f, 60);       // 60 ms-nál: aktuális érték 50 -> onnan indul
    CHECK_NEAR(s.value(60), 50, 1e-3);
    CHECK_NEAR(s.value(110), 125, 1e-3);
}

static void timer_wrap() {
    Smoother s(100);
    s.push(0.f, 0xFFFFFFF0u);
    s.push(100.f, 0xFFFFFFF0u + 0x10u);        // átfordul 0 körül
    // a második minta ideje 0xFFFFFFF0 + 0x10 = 0 (átfordult): onnan indul 0 -> 100
    CHECK_NEAR(s.value(0u), 0, 1e-3);
    CHECK_NEAR(s.value(50u), 50, 1e-3);
    CHECK_NEAR(s.value(100u), 100, 1e-3);
}

int main() {
    RUN(first_sample_no_jump);
    RUN(linear_transition);
    RUN(new_sample_midway);
    RUN(timer_wrap);
    TEST_MAIN_END();
}
