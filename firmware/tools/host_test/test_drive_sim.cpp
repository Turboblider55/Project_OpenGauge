// A menetszimuláció tesztjei, és az együttműködése a fokozatbecsléssel (a szimulátor adatából a modell megtanulja a fokozatokat).
#include <cmath>

#include "drive_sim.h"
#include "gear_model.h"
#include "mini_test.h"

using namespace og;

static void cycle_stays_in_realistic_bounds_and_uses_all_gears() {
    DriveSim s;
    bool seen[DriveSim::kGears + 1] = {};
    bool stopped_again = false, moved = false;
    float max_speed = 0.f;
    for (int i = 0; i < 6000; ++i) {  // 300 s, 20 ms-os lépés
        s.step(0.02f);
        CHECK(s.speed_kmh >= 0.f && s.speed_kmh < 130.f);
        CHECK(s.rpm >= DriveSim::kIdleRpm - 1.f && s.rpm < 4200.f);
        CHECK(s.throttle_pct >= 0.f && s.throttle_pct <= 100.f);
        if (s.gear > 0) seen[s.gear] = true;
        max_speed = std::max(max_speed, s.speed_kmh);
        if (s.speed_kmh > 20.f) moved = true;
        if (moved && s.speed_kmh == 0.f && s.phase == DriveSim::Phase::Stand) stopped_again = true;
        // Bekapcsolt fokozatban a fordulat a sebességgel arányos (a megálláskori alapjárati padló kivételével).
        if (s.gear > 0 && s.speed_kmh > 25.f) CHECK_NEAR(s.rpm, DriveSim::k_of(s.gear) * s.speed_kmh, 1.0);
    }
    for (int g = 1; g <= DriveSim::kGears; ++g) CHECK(seen[g]);
    CHECK(max_speed >= 98.f);
    CHECK(stopped_again);  // a ciklus megáll és újraindul
}

static void model_learns_the_gears_from_the_simulated_drive() {
    DriveSim s;
    GearModel m;
    GearConfig c;
    c.gears = DriveSim::kGears;
    m.set_config(c);
    int right = 0, wrong = 0;
    for (int i = 0; i < 20 * 60 * 10; ++i) {  // 20 perc, 10 Hz
        s.step(0.1f);
        m.update(s.rpm, std::round(s.speed_kmh), true);  // az OBD sebessége egész km/h
        if (m.ready() && s.gear > 0 && s.speed_kmh > 20.f) {
            if (m.gear() == s.gear) ++right;
            else if (m.gear() != 0) ++wrong;  // 0: váltás után a hiszterézis miatt még nem szavazott
        }
    }
    CHECK(m.ready() && m.learned_gears() == DriveSim::kGears);
    for (int g = 1; g <= DriveSim::kGears; ++g) CHECK_NEAR(m.cluster(g - 1).k, DriveSim::k_of(g), DriveSim::k_of(g) * 0.02);
    CHECK(right > 1000);
    CHECK(wrong * 100 < right);  // a téves besorolás 1% alatt
}

static void formula_matches_the_simulated_car() {
    DriveSim s;
    GearModel m;
    GearConfig c;
    c.gears = DriveSim::kGears;
    c.final_drive = DriveSim::kFinal;
    c.tyre_circ_m = DriveSim::kCircM;
    for (int g = 1; g <= DriveSim::kGears; ++g) c.ratio[g - 1] = DriveSim::ratio(g);
    m.set_config(c);
    int right = 0, wrong = 0;
    for (int i = 0; i < 5 * 60 * 10; ++i) {
        s.step(0.1f);
        m.update(s.rpm, std::round(s.speed_kmh), true);  // az OBD sebessége egész km/h
        if (s.gear > 0 && s.speed_kmh > 20.f) {
            if (m.gear() == s.gear) ++right;
            else if (m.gear() != 0) ++wrong;
        }
    }
    CHECK(right > 300);
    CHECK(wrong == 0);
    CHECK(m.learned_gears() == 0);  // a képlet nem tanul
}

int main() {
    RUN(cycle_stays_in_realistic_bounds_and_uses_all_gears);
    RUN(model_learns_the_gears_from_the_simulated_drive);
    RUN(formula_matches_the_simulated_car);
    TEST_MAIN_END();
}
