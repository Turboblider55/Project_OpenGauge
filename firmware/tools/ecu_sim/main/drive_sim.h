#pragma once
// Menetszimuláció az ECU-szimulátorhoz: gyorsítás, állandó tempó, lassítás, fel- és visszaváltások, megállás (hardverfüggetlen).
//
// Mit csinál: egy kézi váltós autó fordulatszámát és sebességét állítja elő úgy, ahogy egy valódi autó mutatná: minden fokozatban
//             a fordulatszám a sebességgel arányos (rpm = k_fokozat * sebesség), váltáskor a tengelykapcsoló nyomva van
//             (a fordulatszám a régi arányról az újra csúszik, a sebesség lassan csökken), megálláskor alapjárat.
// Hogyan:     egy egyszerű menetciklus ismétlődik: állás, gyorsítás 100 km/h-ig, állandó tempó, fékezés 35 km/h-ra,
//             gyorsítás 70 km/h-ra, állandó tempó, fékezés megállásig. Felváltás magas, visszaváltás alacsony
//             fordulatszámnál; a gyorsulás fokozatonként csökken. Minden lépés dt másodperces.
// Miért:      a kijelző fokozatbecsléséhez és a mutatók mozgásának ellenőrzéséhez a konstans/szinuszos érték nem elég:
//             kell valós arányú, váltásokkal tarkított adat. A számok az og_gear_model tesztjeivel is használhatók.
#include <algorithm>
#include <cmath>

namespace og {

struct DriveSim {
    static constexpr int kGears = 5;
    static constexpr float kFinal = 3.9f;         // főhajtás
    static constexpr float kCircM = 1.9f;         // kerékkerület (m)
    static constexpr float kIdleRpm = 850.f;
    static constexpr float kShiftUpRpm = 3300.f;  // felváltás gyorsításkor
    static constexpr float kShiftDownRpm = 1350.f;
    static constexpr float kShiftTimeS = 0.7f;    // a tengelykapcsoló nyomva ennyi ideig

    // Az áttételek (1..5. fokozat).
    static constexpr float ratio(int gear) {
        constexpr float r[kGears] = {3.6f, 2.1f, 1.4f, 1.0f, 0.8f};
        return r[gear - 1];
    }
    // A fokozat áttételi száma: fordulat / sebesség (1/perc per km/h).
    static float k_of(int gear) { return 1000.f / 60.f / kCircM * ratio(gear) * kFinal; }

    enum class Phase { Stand, Accel1, Cruise1, Brake1, Accel2, Cruise2, Brake2 };

    float speed_kmh = 0.f;
    float rpm = kIdleRpm;
    float throttle_pct = 0.f;
    int gear = 0;  // 0: üresben vagy a tengelykapcsoló nyomva (váltás közben, megálláskor)
    Phase phase = Phase::Stand;

    // Mit csinál: dt másodpercnyit léptet a szimuláción.
    void step(float dt) {
        phase_t_ += dt;
        if (shift_left_ > 0.f) {
            step_shift(dt);
            return;
        }
        switch (phase) {
            case Phase::Stand:
                gear = 0;
                throttle_pct = 0.f;
                speed_kmh = 0.f;
                rpm = kIdleRpm;
                if (phase_t_ >= 3.f) go(Phase::Accel1);
                break;
            case Phase::Accel1: accelerate(dt, 100.f, Phase::Cruise1); break;
            case Phase::Cruise1: cruise(dt, 6.f, Phase::Brake1); break;
            case Phase::Brake1: brake(dt, 35.f, Phase::Accel2); break;
            case Phase::Accel2: accelerate(dt, 70.f, Phase::Cruise2); break;
            case Phase::Cruise2: cruise(dt, 5.f, Phase::Brake2); break;
            case Phase::Brake2: brake(dt, 0.f, Phase::Stand); break;
        }
    }

private:
    float phase_t_ = 0.f;
    float shift_left_ = 0.f;
    int shift_to_ = 0;
    float shift_from_k_ = 0.f;

    void go(Phase p) {
        phase = p;
        phase_t_ = 0.f;
    }

    // A gyorsulás (km/h/s) a gázállástól és a fokozattól függ, nagy sebességnél a légellenállás csökkenti.
    static float accel_of(int g, float throttle, float v) {
        constexpr float amax[kGears + 1] = {0.f, 9.f, 6.f, 4.5f, 3.2f, 2.2f};
        return amax[g] * throttle / 100.f - 0.007f * v * v / 100.f;
    }

    // Mit csinál: a tengelykapcsoló nyomva: a fordulat a régi arányról az újra csúszik, közben a sebesség lassan fogy.
    void start_shift(int to) {
        shift_to_ = to;
        shift_left_ = kShiftTimeS;
        shift_from_k_ = gear > 0 ? k_of(gear) : (speed_kmh > 1.f ? rpm / speed_kmh : 0.f);
        gear = 0;
        throttle_pct = 0.f;
    }

    void step_shift(float dt) {
        shift_left_ -= dt;
        speed_kmh = std::max(0.f, speed_kmh - 0.6f * dt);  // gurulás
        const float t = 1.f - std::max(0.f, shift_left_) / kShiftTimeS;
        const float k_to = shift_to_ > 0 ? k_of(shift_to_) : 0.f;
        const float target = shift_to_ > 0 ? k_to * speed_kmh : kIdleRpm;
        const float from = shift_from_k_ * speed_kmh;
        rpm = std::max(kIdleRpm, from + (target - from) * t);
        if (shift_left_ <= 0.f) {
            gear = shift_to_;
            shift_left_ = 0.f;
        }
    }

    void set_rpm_from_gear() {
        const float r = k_of(gear) * speed_kmh;
        rpm = std::max(kIdleRpm, r);
    }

    void accelerate(float dt, float target, Phase next) {
        if (gear == 0) {  // indulás: a kuplung csúszik, a fordulat a 2000 körüli értékig emelkedik
            throttle_pct = 60.f;
            speed_kmh += 7.f * dt;
            rpm = std::max(kIdleRpm, std::min(2300.f, 1100.f + 130.f * speed_kmh));
            if (k_of(1) * speed_kmh >= rpm) {
                gear = 1;
            }
            return;
        }
        throttle_pct = 70.f;
        speed_kmh += std::max(0.f, accel_of(gear, throttle_pct, speed_kmh)) * dt;
        set_rpm_from_gear();
        if (speed_kmh >= target) {
            go(next);
        } else if (rpm >= kShiftUpRpm && gear < kGears) {
            start_shift(gear + 1);
        }
    }

    void cruise(float /*dt*/, float seconds, Phase next) {
        // állandó tempó: a gáz éppen a menetellenállást fedezi
        throttle_pct = 18.f + 0.1f * speed_kmh;
        set_rpm_from_gear();
        if (phase_t_ >= seconds) go(next);
    }

    void brake(float dt, float target, Phase next) {
        throttle_pct = 0.f;
        speed_kmh = std::max(0.f, speed_kmh - 5.5f * dt);
        if (gear > 0) set_rpm_from_gear();
        if (target <= 0.f) {
            if (gear > 1 && rpm <= kShiftDownRpm) {
                start_shift(gear - 1);
            } else if (speed_kmh < 9.f) {  // megállás: kuplung be, a fordulat alapjáratra esik
                gear = 0;
                rpm = std::max(kIdleRpm, rpm - 900.f * dt);
                if (speed_kmh <= 0.f && rpm <= kIdleRpm + 1.f) go(next);
            }
            return;
        }
        if (speed_kmh <= target) {
            go(next);
        } else if (gear > 1 && rpm <= kShiftDownRpm) {
            start_shift(gear - 1);
        }
    }
};

}  // namespace og
