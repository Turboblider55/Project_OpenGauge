// Az állapotjelző LED-minták, a tartóssági szűrő és az akkumulátor-becslés tesztjei.
#include "battery_gauge.h"
#include "led_pattern.h"
#include "mini_test.h"

using namespace og;

static constexpr uint32_t kAfterTest = 10 * kLedAlivePeriodMs;  // az önteszt után, minden minta periódushatárán

static void self_test_shows_each_colour_once() {
    LedState s;
    s.faults = 0xFF;
    CHECK((led_frame(s, 0) == LedOutput{true, false, false}));
    CHECK((led_frame(s, kLedSelfTestStepMs) == LedOutput{false, true, false}));
    CHECK((led_frame(s, 2 * kLedSelfTestStepMs) == LedOutput{false, false, true}));
}

static void green_alive_has_short_gap() {
    LedState s;
    CHECK(!led_frame(s, kAfterTest).green);
    CHECK(led_frame(s, kAfterTest + kLedAliveGapMs).green);
    CHECK(led_frame(s, kAfterTest + kLedAlivePeriodMs - 1).green);
    CHECK(!led_frame(s, kAfterTest).red);
    CHECK(!led_frame(s, kAfterTest).blue);
}

static void green_search_blinks_slowly() {
    LedState s;
    s.searching = true;
    CHECK(led_frame(s, kAfterTest).green);
    CHECK(!led_frame(s, kAfterTest + kLedSearchPeriodMs / 2).green);
}

static void green_blinks_on_severe_fault_only() {
    LedState s;
    s.faults = fault_bit(Fault::Radio);  // nem súlyos: marad a normál fény
    CHECK(led_frame(s, kAfterTest + kLedSeverePeriodMs / 2 + 100).green);
    s.faults |= fault_bit(Fault::Init);  // súlyos: 1 Hz
    CHECK(led_frame(s, kAfterTest).green);
    CHECK(!led_frame(s, kAfterTest + kLedSeverePeriodMs / 2).green);
}

static void blue_follows_link_state() {
    LedState s;
    s.link = LinkLed::Off;
    CHECK(!led_frame(s, kAfterTest).blue);
    s.link = LinkLed::Waiting;
    CHECK(led_frame(s, kAfterTest).blue);
    CHECK(!led_frame(s, kAfterTest + kLedWaitPeriodMs / 2).blue);
    s.link = LinkLed::Ok;
    CHECK(led_frame(s, kAfterTest + kLedWaitPeriodMs / 2).blue);
}

// A sorozat bekapcsolásainak száma egy periódusban egyenlő a hibakód számértékével.
static void red_flash_count_equals_code() {
    for (uint8_t code = 1; code <= 6; ++code) {
        const Fault f = static_cast<Fault>(code);
        const uint32_t period = 2u * code * kLedCodeStepMs + kLedCodePauseMs;
        int rises = 0;
        bool prev = false;
        for (uint32_t t = 0; t < period; t += 10) {
            const bool on = red_code_on(f, t);
            if (on && !prev) ++rises;
            prev = on;
        }
        CHECK(rises == code);
    }
    CHECK(!red_code_on(Fault::None, 0));
}

static void top_fault_prefers_most_serious() {
    CHECK(top_fault(0) == Fault::None);
    CHECK(top_fault(fault_bit(Fault::Reset) | fault_bit(Fault::Radio)) == Fault::Radio);
    CHECK(top_fault(fault_bit(Fault::Ecu) | fault_bit(Fault::Power)) == Fault::Power);
    CHECK(top_fault(fault_bit(Fault::Ecu) | fault_bit(Fault::Can)) == Fault::Can);
    CHECK(top_fault(fault_bit(Fault::Can) | fault_bit(Fault::Init)) == Fault::Init);
}

static void persist_ignores_short_dropouts() {
    PersistFilter f(3000, 1000);
    CHECK(!f.update(true, 0));
    CHECK(!f.update(true, 2999));
    CHECK(!f.update(false, 3000));  // megszakadt: a számlálás újraindul
    CHECK(!f.update(true, 3100));
    CHECK(!f.update(true, 6099));
    CHECK(f.update(true, 6100));
    CHECK(f.update(false, 6200));   // visszaállás: 1 s kell hozzá
    CHECK(f.update(true, 6300));    // egy jó minta után ismét hibás: marad
    CHECK(f.update(false, 6400));
    CHECK(!f.update(false, 7400));
}

static void percent_curve_limits_and_monotonic() {
    CHECK(BatteryGauge::percent_from_mv(3000) == 0);
    CHECK(BatteryGauge::percent_from_mv(3400) == 0);
    CHECK(BatteryGauge::percent_from_mv(4200) == 100);
    CHECK(BatteryGauge::percent_from_mv(4350) == 100);
    CHECK(BatteryGauge::percent_from_mv(3750) == 51);
    uint8_t prev = 0;
    for (uint16_t mv = 3300; mv <= 4300; mv += 5) {
        const uint8_t p = BatteryGauge::percent_from_mv(mv);
        CHECK(p >= prev);
        prev = p;
    }
}

// Addig eteti a mérővel a feszültséget, amíg a szűrő beáll.
static uint32_t settle(BatteryGauge& g, uint16_t mv, uint32_t t) {
    for (int i = 0; i < 200; ++i, t += 1000) g.feed(mv, t);
    return t;
}

static void level_has_hysteresis() {
    BatteryGauge g;
    uint32_t t = settle(g, 3800, 0);
    CHECK(g.status().level == BatteryLevel::Ok);
    t = settle(g, 3480, t);
    CHECK(g.status().level == BatteryLevel::Low);
    t = settle(g, 3520, t);  // a határ fölött, de a hiszterézisen belül
    CHECK(g.status().level == BatteryLevel::Low);
    t = settle(g, 3560, t);
    CHECK(g.status().level == BatteryLevel::Ok);
    t = settle(g, 3380, t);
    CHECK(g.status().level == BatteryLevel::Critical);
    t = settle(g, 3420, t);
    CHECK(g.status().level == BatteryLevel::Critical);
    t = settle(g, 3460, t);
    CHECK(g.status().level == BatteryLevel::Low);
    settle(g, 100, t);
    CHECK(g.status().level == BatteryLevel::Absent);
}

static void filter_ignores_single_spike() {
    BatteryGauge g;
    uint32_t t = settle(g, 3800, 0);
    g.feed(3200, t);
    CHECK(g.status().mv >= 3700);
    CHECK(g.status().level == BatteryLevel::Ok);
}

static void charging_estimated_from_rising_voltage() {
    BatteryGauge g;
    uint32_t t = settle(g, 3700, 0);
    CHECK(!g.status().charging);
    for (uint16_t mv = 3700; mv < 3800; mv += 5, t += 10000) g.feed(mv, t);
    t = settle(g, 3850, t);
    CHECK(g.status().charging);
    for (int i = 0; i < 3; ++i) t = settle(g, static_cast<uint16_t>(3840 - 20 * i), t);
    CHECK(!g.status().charging);
}

int main() {
    RUN(self_test_shows_each_colour_once);
    RUN(green_alive_has_short_gap);
    RUN(green_search_blinks_slowly);
    RUN(green_blinks_on_severe_fault_only);
    RUN(blue_follows_link_state);
    RUN(red_flash_count_equals_code);
    RUN(top_fault_prefers_most_serious);
    RUN(persist_ignores_short_dropouts);
    RUN(percent_curve_limits_and_monotonic);
    RUN(level_has_hysteresis);
    RUN(filter_ignores_single_spike);
    RUN(charging_estimated_from_rising_voltage);
    TEST_MAIN_END();
}
