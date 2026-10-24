#pragma once
// A ritka (pl. 10 Hz) mintákból folyamatos értéket állít elő: az előző megjelenített
// értékről az újra egy mintaperiódus alatt lineárisan halad. Ára: legfeljebb egy
// mintányi késés. Tiszta logika, hardver nélkül tesztelhető.
#include <cstdint>

namespace og {

class Smoother {
public:
    explicit Smoother(uint32_t period_ms = 100) : period_ms_(period_ms ? period_ms : 1) {}

    void set_period(uint32_t period_ms) { period_ms_ = period_ms ? period_ms : 1; }

    // Új mérés érkezett t_ms időpontban. Az első minta nem ugrik nulláról.
    void push(float v, uint32_t t_ms) {
        from_ = started_ ? value(t_ms) : v;
        to_ = v;
        t0_ = t_ms;
        started_ = true;
    }

    // A megjelenítendő érték t_ms időpontban.
    float value(uint32_t t_ms) const {
        if (!started_) return 0.f;
        uint32_t dt = t_ms - t0_;  // előjel nélküli: az időátfordulást is kezeli
        if (dt >= period_ms_) return to_;
        return from_ + (to_ - from_) * static_cast<float>(dt) / static_cast<float>(period_ms_);
    }

private:
    uint32_t period_ms_;
    uint32_t t0_ = 0;
    float from_ = 0.f, to_ = 0.f;
    bool started_ = false;
};

}  // namespace og
