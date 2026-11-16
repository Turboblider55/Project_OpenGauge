#pragma once
// LiPo-akkumulátor töltöttségének becslése a cellafeszültségből (hardverfüggetlen, PC-n tesztelhető).
//
// Mit csinál: a mért feszültséget szűri, százalékra váltja, megadja a szintet (rendben, alacsony,
//             kritikus), és a feszültség emelkedéséből megbecsüli, hogy tölt-e a cella.
// Hogyan:     exponenciális átlag; százalék szakaszonként lineáris táblázatból, kis lépésű
//             hiszterézissel; szintek hiszterézissel; töltésbecslés percenkénti összehasonlítással.
// Miért:      a kijelző-NYÁK-on a TP4056 CHRG és STDBY kimenete nem jut el a mikrovezérlőhöz, és
//             VBUS-érzékelés sincs, ezért a töltés ténye csak a feszültségből következtethető ki.
//             Töltés közben a kapocsfeszültség a nyugalminál magasabb, a százalék ilyenkor túlzó.
#include <cstddef>
#include <cstdint>

namespace og {

enum class BatteryLevel : uint8_t {
    Absent,    // nincs cella (a mért feszültség nem lehet valódi cella feszültsége)
    Ok,
    Low,       // V_BAT < 3,5 V: USB kihúzásakor már újraindulás várható (display leírás 3.2)
    Critical,  // V_BAT < 3,4 V: az LDO kiesik a szabályozásból, a 3,3 V-os sín követi a cellát
};

struct BatteryStatus {
    bool valid = false;           // volt-e már mérés
    uint16_t mv = 0;              // szűrt cellafeszültség
    uint8_t percent = 0;          // 0..100
    BatteryLevel level = BatteryLevel::Absent;
    bool charging = false;        // becslés: a feszültség emelkedik
};

class BatteryGauge {
public:
    static constexpr uint16_t kAbsentMv = 2500;        // ez alatt nincs cella (a DW01A is kb. 2,4 V-nál old)
    static constexpr uint16_t kLowMv = 3500;
    static constexpr uint16_t kCriticalMv = 3400;
    static constexpr uint16_t kHysteresisMv = 50;      // a szint ennyivel a határ fölött áll vissza
    static constexpr uint8_t kPercentStep = 2;         // kisebb változásra a kijelzett érték nem mozdul
    // A töltésbecslés időablaka: 500 mA-es töltésnél a görbe lapos szakaszán a feszültség kb.
    // 10 mV/perc sebességgel emelkedik, ezért rövidebb ablakban a jel elveszne a mérés zajában.
    static constexpr uint32_t kTrendPeriodMs = 120000;
    static constexpr int kChargeRiseMv = 20;           // legalább ennyi emelkedés: tölt
    static constexpr int kChargeFallMv = 10;           // ennyi esés: nem tölt

    // Mit csinál: a szakaszonként lineáris feszültség-százalék görbe (tipikus LiPo-cella).
    // Miért:      a 0 % a 3,4 V: ez alatt az LDO már nem szabályoz, a cella többi része nem
    //             hasznosítható. A táblázat irodalmi jellegű, nem az adott cellán mért görbe.
    static uint8_t percent_from_mv(uint16_t mv) {
        struct Point {
            uint16_t mv;
            uint8_t pct;
        };
        static constexpr Point kCurve[] = {{3400, 0},  {3500, 5},  {3600, 12}, {3700, 40}, {3800, 62},
                                           {3900, 75}, {4000, 85}, {4100, 93}, {4200, 100}};
        if (mv <= kCurve[0].mv) return 0;
        for (size_t i = 1; i < sizeof kCurve / sizeof kCurve[0]; ++i) {
            if (mv <= kCurve[i].mv) {
                const uint32_t dx = static_cast<uint32_t>(mv - kCurve[i - 1].mv);
                const uint32_t span = static_cast<uint32_t>(kCurve[i].mv - kCurve[i - 1].mv);
                const uint32_t dp = static_cast<uint32_t>(kCurve[i].pct - kCurve[i - 1].pct);
                return static_cast<uint8_t>(kCurve[i - 1].pct + (dx * dp + span / 2) / span);
            }
        }
        return 100;
    }

    // Mit csinál: egy új mérést dolgoz fel (mv: a cella feszültsége, now_ms: a mérés ideje).
    // Hogyan:     az első mérés közvetlenül beáll, a többi 1/16 súllyal számít (a mért zaj a
    //             NYÁK-on 16 minta átlagával is ±80 mV körüli volt).
    void feed(uint16_t mv, uint32_t now_ms) {
        if (!status_.valid) {
            filtered_ = static_cast<int32_t>(mv) * 16;
            status_.valid = true;
            status_.percent = percent_from_mv(mv);
            trend_mv_ = mv;
            trend_ms_ = now_ms;
        } else {
            filtered_ += (static_cast<int32_t>(mv) * 16 - filtered_) / 16;
        }
        status_.mv = static_cast<uint16_t>(filtered_ / 16);
        update_percent();
        update_level();
        update_trend(now_ms);
    }

    const BatteryStatus& status() const { return status_; }

private:
    void update_percent() {
        const uint8_t p = percent_from_mv(status_.mv);
        const int d = static_cast<int>(p) - status_.percent;
        if (d >= kPercentStep || d <= -static_cast<int>(kPercentStep) || p == 0 || p == 100) status_.percent = p;
    }

    void update_level() {
        const uint16_t mv = status_.mv;
        BatteryLevel& l = status_.level;
        if (mv < kAbsentMv) {
            l = BatteryLevel::Absent;
        } else if (mv < kCriticalMv) {
            l = BatteryLevel::Critical;
        } else if (mv < kLowMv) {
            if (l != BatteryLevel::Critical || mv >= kCriticalMv + kHysteresisMv) l = BatteryLevel::Low;
        } else if (l == BatteryLevel::Absent || l == BatteryLevel::Ok || mv >= kLowMv + kHysteresisMv) {
            l = BatteryLevel::Ok;
        }
    }

    void update_trend(uint32_t now_ms) {
        if (now_ms - trend_ms_ < kTrendPeriodMs) return;
        const int d = static_cast<int>(status_.mv) - trend_mv_;
        if (d >= kChargeRiseMv) status_.charging = true;
        else if (d <= -kChargeFallMv) status_.charging = false;
        trend_mv_ = status_.mv;
        trend_ms_ = now_ms;
    }

    BatteryStatus status_;
    int32_t filtered_ = 0;  // 1/16 mV-ban
    int trend_mv_ = 0;
    uint32_t trend_ms_ = 0;
};

}  // namespace og
