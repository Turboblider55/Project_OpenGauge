// A fokozatbecslés tesztjei: tanulás, képletes számítás, zaj, korlátok, csere, mentés, váltások.
#include <cstring>

#include "gear_model.h"
#include "mini_test.h"

using namespace og;

namespace {

// Egy 5 fokozatú autó: főhajtás 3,9, kerékkerület 1,9 m. A k = 16,667 / kerület * áttétel * főhajtás.
constexpr float kFinal = 3.9f, kCirc = 1.9f;
constexpr float kRatio[5] = {3.6f, 2.1f, 1.4f, 1.0f, 0.8f};

float k_of(int gear) { return 1000.f / 60.f / kCirc * kRatio[gear - 1] * kFinal; }

// Egyszerű, determinisztikus zaj (-1..1), hogy a tesztek ismételhetők legyenek.
struct Noise {
    uint32_t s = 12345;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>((s >> 8) & 0xFFFF) / 32768.f - 1.f;
    }
};

// Mit csinál: adott fokozatban, a sebességet from-ról to-ra növelve, 10 Hz-en adja a mintákat (a zaj relatív).
void drive(GearModel& m, Noise& n, int gear, float from, float to, float seconds, float noise = 0.f) {
    const int steps = static_cast<int>(seconds * 10);
    for (int i = 0; i < steps; ++i) {
        const float v = from + (to - from) * static_cast<float>(i) / static_cast<float>(steps > 1 ? steps - 1 : 1);
        m.update(k_of(gear) * v * (1.f + noise * n.next()), std::round(v), true);  // az OBD sebessége egész km/h
    }
}

// Mit csinál: váltást utánoz: a fordulatszám a régi fokozat k-jától az újig csúszik 0,5 s alatt (a tengelykapcsoló nyomva).
void shift(GearModel& m, int from_gear, int to_gear, float speed) {
    for (int i = 0; i < 5; ++i) {
        const float t = static_cast<float>(i) / 4.f;
        const float k = k_of(from_gear) + (k_of(to_gear) - k_of(from_gear)) * t;
        m.update(k * speed * (1.f + 0.2f * t * (1.f - t)), std::round(speed), true);  // a közbenső értékek nem állnak be fokozatra
    }
}

// Egy teljes gyorsítás 1.-től 5.-ig, váltásokkal.
void accelerate_through_gears(GearModel& m, Noise& n, float noise = 0.f) {
    drive(m, n, 1, 18, 28, 3, noise); shift(m, 1, 2, 28);
    drive(m, n, 2, 28, 45, 3, noise); shift(m, 2, 3, 45);
    drive(m, n, 3, 45, 65, 3, noise); shift(m, 3, 4, 65);
    drive(m, n, 4, 65, 90, 3, noise); shift(m, 4, 5, 90);
    drive(m, n, 5, 90, 120, 3, noise);
}

}  // namespace

static void learns_all_gears_and_reports_them() {
    GearModel m;
    GearConfig c;
    c.gears = 5;
    m.set_config(c);
    Noise n;
    accelerate_through_gears(m, n);
    CHECK(m.learned_gears() == 5 && m.ready());
    for (int g = 1; g <= 5; ++g) {
        drive(m, n, g, 50, 51, 1);  // 1 s egy fokozatban
        CHECK(m.gear() == g);
    }
    for (int g = 1; g <= 5; ++g) CHECK_NEAR(m.cluster(g - 1).k, k_of(g), k_of(g) * 0.02);
}

static void no_gear_reported_until_every_gear_is_known() {
    GearModel m;
    GearConfig c;
    c.gears = 5;
    m.set_config(c);
    Noise n;
    drive(m, n, 3, 45, 65, 3); shift(m, 3, 4, 65);
    drive(m, n, 4, 65, 90, 3);
    CHECK(m.learned_gears() == 2 && !m.ready());
    CHECK(m.gear() == 0);  // a 3. és 4. fokozat nem lehet "1." és "2."
}

static void tolerates_noise() {
    GearModel m;
    GearConfig c;
    c.gears = 5;
    m.set_config(c);
    Noise n;
    for (int i = 0; i < 3; ++i) accelerate_through_gears(m, n, 0.005f);  // ±0,5% zaj
    CHECK(m.learned_gears() == 5);
    drive(m, n, 4, 70, 71, 1, 0.005f);
    CHECK(m.gear() == 4);
}

static void ignores_slow_speed_and_clutch_transitions() {
    GearModel m;
    GearConfig c;
    c.gears = 5;
    m.set_config(c);
    Noise n;
    drive(m, n, 1, 1, 4, 5);  // 5 km/h alatt: nincs fokozat, nincs tanulás
    CHECK(m.learned_gears() == 0 && m.gear() == 0);
    for (int i = 0; i < 100; ++i) m.update(1500.f + 100.f * static_cast<float>(i % 30), 40.f, true);  // a fordulat pörög (kuplung nyomva), a sebesség áll: nem stabil
    CHECK(m.learned_gears() == 0);
    m.update(3000.f, 60.f, false);  // érvénytelen minta
    CHECK(m.gear() == 0);
}

static void single_glitch_does_not_change_output() {
    GearModel m;
    GearConfig c;
    c.gears = 5;
    m.set_config(c);
    Noise n;
    accelerate_through_gears(m, n);
    drive(m, n, 3, 50, 52, 1);
    CHECK(m.gear() == 3);
    m.update(k_of(4) * 50.f, 50.f, true);  // egy hibás minta
    CHECK(m.gear() == 3);
    drive(m, n, 3, 50, 52, 0.5f);
    CHECK(m.gear() == 3);
}

static void formula_mode_needs_no_learning() {
    GearModel m;
    GearConfig c;
    c.gears = 5;
    c.final_drive = kFinal;
    c.tyre_circ_m = kCirc;
    for (int i = 0; i < 5; ++i) c.ratio[i] = kRatio[i];
    CHECK(c.has_formula());
    m.set_config(c);
    CHECK(m.ready());
    Noise n;
    for (int g = 1; g <= 5; ++g) {
        drive(m, n, g, 40, 41, 1);
        CHECK(m.gear() == g);
    }
    CHECK(m.learned_gears() == 0 && m.samples() == 0);  // képlet mellett nem tanul
    c.ratio[2] = 0;  // hiányos adat: nincs képlet
    CHECK(!c.has_formula());
}

static void learning_is_bounded_and_freezes() {
    GearModel m;
    GearConfig c;
    c.gears = 5;
    m.set_config(c);
    Noise n;
    int guard = 0;
    while (!m.frozen() && guard++ < 2000) accelerate_through_gears(m, n);  // sok vezetés
    CHECK(m.frozen());
    CHECK(m.samples() <= GearModel::kFreezeSamples);
    for (int i = 0; i < 5; ++i) CHECK(m.cluster(i).n <= GearModel::kMaxCount);
    // Lezárás után egy új, idegen áttételi szám nem tanulható.
    const int before = m.learned_gears();
    for (int i = 0; i < 400; ++i) m.update(2000.f, 60.f, true);
    CHECK(m.learned_gears() == before && m.samples() == GearModel::kFreezeSamples);
}

static void promising_candidate_replaces_weakest_cluster() {
    GearModel m;
    GearConfig c;
    c.gears = 3;
    m.set_config(c);
    Noise n;
    // Három fürt: egy gyenge (csak egyszer látott 1. fokozat) és két erős.
    drive(m, n, 1, 20, 25, 1);
    for (int i = 0; i < 6; ++i) { drive(m, n, 3, 45, 65, 3); drive(m, n, 4, 65, 90, 3); shift(m, 4, 3, 60); }
    CHECK(m.learned_gears() == 3);
    const float weak_k = m.cluster(0).k;
    CHECK_NEAR(weak_k, k_of(1), k_of(1) * 0.02);
    // Új, ígéretesebb áttételi szám (2. fokozat) sokszor: leváltja a gyengét.
    for (int i = 0; i < 10; ++i) { drive(m, n, 2, 28, 45, 3); shift(m, 2, 3, 45); }
    bool has2 = false, has1 = false;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(m.cluster(i).k - k_of(2)) < k_of(2) * 0.02) has2 = true;
        if (std::fabs(m.cluster(i).k - k_of(1)) < k_of(1) * 0.02) has1 = true;
    }
    CHECK(has2 && !has1);
    // Az erős fürtök megmaradtak.
    int strong = 0;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(m.cluster(i).k - k_of(3)) < k_of(3) * 0.02 || std::fabs(m.cluster(i).k - k_of(4)) < k_of(4) * 0.02) ++strong;
    CHECK(strong == 2);
}

static void persistence_round_trip_and_damage() {
    GearModel a;
    GearConfig c;
    c.gears = 5;
    a.set_config(c);
    Noise n;
    accelerate_through_gears(a, n);
    CHECK(a.take_dirty());
    CHECK(!a.take_dirty());  // a jelző törlődik
    uint8_t blob[GearModel::kBlobSize];
    CHECK(a.encode(blob, sizeof blob) == GearModel::kBlobSize);
    GearModel b;
    CHECK(b.decode(blob, sizeof blob));
    CHECK(b.learned_gears() == 5 && b.ready() && b.samples() == a.samples());
    for (int i = 0; i < 5; ++i) CHECK_NEAR(b.cluster(i).k, a.cluster(i).k, 0.02);
    drive(b, n, 4, 70, 71, 1);
    CHECK(b.gear() == 4);  // a visszatöltött adat azonnal használható

    GearModel d;
    uint8_t bad[GearModel::kBlobSize];
    std::memcpy(bad, blob, sizeof blob); bad[10] ^= 0x40;
    CHECK(!d.decode(bad, sizeof bad));  // összeghiba
    std::memcpy(bad, blob, sizeof blob); bad[0] = 'X';
    CHECK(!d.decode(bad, sizeof bad));
    CHECK(!d.decode(blob, sizeof blob - 1));
    CHECK(!d.decode(nullptr, 0));
    CHECK(d.learned_gears() == 0);  // hibás adat nem módosít
    uint8_t small[GearModel::kBlobSize - 1];
    CHECK(a.encode(small, sizeof small) == 0);
}

static void reset_clears_learning() {
    GearModel m;
    GearConfig c;
    c.gears = 5;
    m.set_config(c);
    Noise n;
    accelerate_through_gears(m, n);
    m.reset_learning();
    CHECK(m.learned_gears() == 0 && m.samples() == 0 && m.gear() == 0 && !m.ready());
    CHECK(m.take_dirty());  // a törlést menteni kell
}

int main() {
    RUN(learns_all_gears_and_reports_them);
    RUN(no_gear_reported_until_every_gear_is_known);
    RUN(tolerates_noise);
    RUN(ignores_slow_speed_and_clutch_transitions);
    RUN(single_glitch_does_not_change_output);
    RUN(formula_mode_needs_no_learning);
    RUN(learning_is_bounded_and_freezes);
    RUN(promising_candidate_replaces_weakest_cluster);
    RUN(persistence_round_trip_and_damage);
    RUN(reset_clears_learning);
    TEST_MAIN_END();
}
