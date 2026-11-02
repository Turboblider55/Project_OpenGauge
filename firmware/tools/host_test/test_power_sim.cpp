// Az alvási állapotgép és az ECU-szimulátor konzolparancsainak tesztjei.
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
    TEST_MAIN_END();
}
