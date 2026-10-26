#pragma once
// Vételi statisztika a sorszámok alapján (kijelzőoldal).
//
// Mit csinál: számolja a vett, elveszett és elavult (duplikált vagy késve érkezett) csomagokat,
//             és felismeri, ha az adó újraindult (a sorszáma visszaugrott).
// Hogyan:     az og_seq.h félkörös összehasonlítására épül. Ha egymás után legalább kResyncAfter
//             csomag "régebbi" a legutóbbinál, az adó újraindult: a számláló az új sorszámhoz
//             igazodik, az addigi statisztika megmarad.
// Miért:      a csomagvesztés mérése (M4) és a kijelzőn az "elavult" adat felismerése ezen
//             múlik; az újraindulás felismerése nélkül egy újraindított OBD-egység összes
//             csomagját elavultnak látnánk. Hardverfüggetlen, PC-n tesztelhető.
#include <cstdint>

#include "og_seq.h"

namespace og {

class LinkStats {
public:
    static constexpr uint8_t kResyncAfter = 3;

    // Mit csinál: egy vett csomag sorszámát dolgozza fel. Igaz, ha a csomag új (felhasználható).
    bool on_seq(uint16_t seq) {
        if (!has_last_) {
            accept(seq);
            return true;
        }
        if (!seq_is_newer(last_, seq)) {
            ++stale_;
            if (++stale_streak_ < kResyncAfter) return false;
            ++resyncs_;  // az adó újraindult: az új sorszámsorozathoz igazodunk
            accept(seq);
            return true;
        }
        lost_ += seq_gap(last_, seq);
        accept(seq);
        return true;
    }

    uint32_t received() const { return received_; }
    uint32_t lost() const { return lost_; }
    uint32_t stale() const { return stale_; }
    uint32_t resyncs() const { return resyncs_; }
    uint16_t last_seq() const { return last_; }

    // Elveszett / (vett + elveszett), 0..1.
    float loss_ratio() const {
        const uint32_t total = received_ + lost_;
        return total ? static_cast<float>(lost_) / static_cast<float>(total) : 0.f;
    }

private:
    void accept(uint16_t seq) {
        last_ = seq;
        has_last_ = true;
        stale_streak_ = 0;
        ++received_;
    }

    uint32_t received_ = 0, lost_ = 0, stale_ = 0, resyncs_ = 0;
    uint16_t last_ = 0;
    uint8_t stale_streak_ = 0;
    bool has_last_ = false;
};

}  // namespace og
