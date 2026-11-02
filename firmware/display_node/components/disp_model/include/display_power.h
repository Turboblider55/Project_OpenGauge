#pragma once
// A kijelzőegység alvási döntése (hardverfüggetlen, PC-n tesztelhető).
//
// Mit csinál: eldönti, mikor aludjon el a kijelző: ha egy beállított ideig nincs sem érintés, sem
//             friss adat; ha az OBD-egység a motor leállását jelezte, a visszaszámlálás a jelzéstől
//             (vagy a legutóbbi érintéstől) indul.
// Hogyan:     a "vonatkoztatási idő" az utolsó érintés, az utolsó ébredés és az utolsó adat (motor
//             leállt jelzés esetén a jelzés ideje) közül a legkésőbbi; az alvásig hátralévő idő ebből
//             számolódik. Az indulási türelmi idő alatt nem alszik el (programozás).
// Miért:      a vezető a motor lefulladása után újra tudja indítani az autót, és a kijelző addig
//             ne aludjon el; érintésre a visszaszámlálás újraindul, a lapozgatás közben sem alszik el.
#include <cstdint>

namespace og {

struct DisplayPowerConfig {
    uint32_t sleep_after_ms = 20000;  // ennyi tétlenség után alszik el
    uint32_t startup_grace_ms = 0;    // indulás után ennyi ideig nem alszik el
};

class DisplayPowerFsm {
public:
    // A "nincs adat" jelzés és a visszaszámlálás megjelenítése ennyi adat nélküli idő után kezdődik.
    static constexpr uint32_t kNoDataMs = 2000;

    explicit DisplayPowerFsm(const DisplayPowerConfig& cfg = DisplayPowerConfig()) : cfg_(cfg) {}

    void on_touch(uint32_t now_ms) { if (now_ms > last_touch_ms_) last_touch_ms_ = now_ms; }
    void on_data(uint32_t now_ms) { last_data_ms_ = now_ms; engine_stopped_ = false; }
    void on_wake(uint32_t now_ms) { wake_ms_ = now_ms; }

    // Mit csinál: a modell "motor leállt" jelzését veszi át (a jelzés ideje a visszaszámlálás kezdete).
    void sync_engine(bool stopped, uint32_t stopped_ms) {
        engine_stopped_ = stopped;
        if (stopped) stopped_ms_ = stopped_ms;
    }

    bool should_sleep(uint32_t now_ms) const {
        return now_ms >= cfg_.startup_grace_ms && now_ms - reference() >= cfg_.sleep_after_ms;
    }

    // Mit csinál: igaz, ha érdemes a visszaszámlálást mutatni (a motor leállt, vagy régóta nincs adat).
    bool counting_down(uint32_t now_ms) const {
        return engine_stopped_ || now_ms - last_data_ms_ >= kNoDataMs;
    }

    // Mit csinál: hány másodperc van az alvásig (felfelé kerekítve, legalább 0).
    int seconds_left(uint32_t now_ms) const {
        const uint32_t elapsed = now_ms - reference();
        if (elapsed >= cfg_.sleep_after_ms) return 0;
        return static_cast<int>((cfg_.sleep_after_ms - elapsed + 999) / 1000);
    }

    bool engine_stopped() const { return engine_stopped_; }

private:
    uint32_t reference() const {
        uint32_t r = last_touch_ms_ > wake_ms_ ? last_touch_ms_ : wake_ms_;
        const uint32_t d = engine_stopped_ ? stopped_ms_ : last_data_ms_;
        return d > r ? d : r;
    }

    DisplayPowerConfig cfg_;
    uint32_t last_touch_ms_ = 0, last_data_ms_ = 0, wake_ms_ = 0, stopped_ms_ = 0;
    bool engine_stopped_ = false;
};

}  // namespace og
