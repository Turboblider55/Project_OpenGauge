// Az alvási állapotgép és az ECU-szimulátor konzolparancsainak tesztjei.
#include "display_power.h"
#include "mini_test.h"
#include "power_fsm.h"
#include "sim_command.h"

using namespace og;

static void sleeps_only_after_idle_time() {
    PowerFsm p;
    p.on_activity(1000);
    CHECK(!p.should_sleep(1000 + p.config().idle_to_sleep_ms - 1));
    CHECK(p.should_sleep(1000 + p.config().idle_to_sleep_ms));
}

static void startup_grace_delays_first_sleep() {
    PowerConfig c;
    c.startup_grace_ms = 60000;
    PowerFsm p(c);
    p.on_activity(0);
    CHECK(!p.should_sleep(30000));  // az üresjárati idő letelt, de az indulási türelmi idő nem
    CHECK(p.should_sleep(60000));
}

static void activity_postpones_sleep() {
    PowerFsm p;
    p.on_activity(0);
    p.on_activity(8000);
    CHECK(!p.should_sleep(12000));
    CHECK(p.should_sleep(18000));
}

static void quiet_windows_keep_sleeping() {
    PowerFsm p;
    p.enter_sleep();
    CHECK(!p.should_sleep(1000000));  // alvó állapotban nincs "elalvás"
    CHECK(!p.on_window(false, 1000));
    CHECK(!p.on_window(false, 2000));
    CHECK(p.state() == PowerState::Sleeping);
    CHECK(p.windows() == 2);
}

static void bus_activity_wakes_and_resets_idle_timer() {
    PowerFsm p;
    p.enter_sleep();
    CHECK(!p.on_window(false, 1000));
    CHECK(p.on_window(true, 2000));
    CHECK(p.state() == PowerState::Active);
    CHECK(!p.should_sleep(2000 + p.config().idle_to_sleep_ms - 1));
    CHECK(p.windows() == 2);
}

static void window_outside_sleep_is_ignored() {
    PowerFsm p;
    CHECK(!p.on_window(true, 0));
    CHECK(p.windows() == 0);
}

static void parses_key_and_auto() {
    CHECK(parse_sim_command("key on").cmd == SimCmd::KeyOn);
    CHECK(parse_sim_command("key off\r\n").cmd == SimCmd::KeyOff);
    CHECK(parse_sim_command("auto on").cmd == SimCmd::AutoOn);
    CHECK(parse_sim_command("auto off").cmd == SimCmd::AutoOff);
    CHECK(parse_sim_command("key").cmd == SimCmd::Bad);
    CHECK(parse_sim_command("key maybe").cmd == SimCmd::Bad);
}

static void parses_values() {
    SimCommand c = parse_sim_command("rpm 3250");
    CHECK(c.cmd == SimCmd::Set);
    CHECK(c.field == SimField::Rpm);
    CHECK(c.value == 3250.f);
    c = parse_sim_command("  vbat   12.6 ");
    CHECK(c.cmd == SimCmd::Set);
    CHECK(c.field == SimField::Vbat);
    CHECK(c.value > 12.59f && c.value < 12.61f);
    CHECK(parse_sim_command("speed abc").cmd == SimCmd::Bad);
    CHECK(parse_sim_command("speed").cmd == SimCmd::Bad);
    CHECK(parse_sim_command("foo 1").cmd == SimCmd::Bad);
}

static void parses_empty_help_status() {
    CHECK(parse_sim_command("").cmd == SimCmd::None);
    CHECK(parse_sim_command("\r\n").cmd == SimCmd::None);
    CHECK(parse_sim_command("help").cmd == SimCmd::Help);
    CHECK(parse_sim_command("status").cmd == SimCmd::Status);
}

static void display_sleeps_after_idle_without_data() {
    DisplayPowerFsm f;
    f.on_wake(0);
    CHECK(!f.should_sleep(19999));
    CHECK(f.should_sleep(20000));
}

static void display_data_keeps_awake_and_touch_restarts_countdown() {
    DisplayPowerFsm f;
    f.on_data(30000);
    CHECK(!f.should_sleep(49999));  // az adat a vonatkoztatási idő
    CHECK(f.should_sleep(50000));
    f.on_touch(45000);
    CHECK(!f.should_sleep(64999));  // az érintés újraindítja a visszaszámlálást
    CHECK(f.should_sleep(65000));
}

static void display_engine_stopped_starts_countdown_from_message() {
    DisplayPowerFsm f;
    f.on_data(100000);
    f.sync_engine(true, 105000);
    CHECK(f.counting_down(105000));
    CHECK(f.seconds_left(105000) == 20);
    CHECK(f.seconds_left(115500) == 10);
    CHECK(!f.should_sleep(124999));
    CHECK(f.should_sleep(125000));
    f.on_data(110000);  // a motor újraindult: az új adat törli a jelzést
    CHECK(!f.engine_stopped());
    CHECK(!f.should_sleep(125000));
}

static void display_grace_period_blocks_early_sleep() {
    DisplayPowerConfig c;
    c.startup_grace_ms = 60000;
    DisplayPowerFsm f(c);
    CHECK(!f.should_sleep(30000));
    CHECK(f.should_sleep(60000));
}

static void display_countdown_shown_only_without_data() {
    DisplayPowerFsm f;
    f.on_data(10000);
    CHECK(!f.counting_down(11000));
    CHECK(f.counting_down(12000));
}

int main() {
    RUN(sleeps_only_after_idle_time);
    RUN(startup_grace_delays_first_sleep);
    RUN(activity_postpones_sleep);
    RUN(quiet_windows_keep_sleeping);
    RUN(bus_activity_wakes_and_resets_idle_timer);
    RUN(window_outside_sleep_is_ignored);
    RUN(parses_key_and_auto);
    RUN(parses_values);
    RUN(parses_empty_help_status);
    RUN(display_sleeps_after_idle_without_data);
    RUN(display_data_keeps_awake_and_touch_restarts_countdown);
    RUN(display_engine_stopped_starts_countdown_from_message);
    RUN(display_grace_period_blocks_early_sleep);
    RUN(display_countdown_shown_only_without_data);
    TEST_MAIN_END();
}
