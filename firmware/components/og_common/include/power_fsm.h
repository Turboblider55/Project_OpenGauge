#pragma once
// Az OBD-egység alvási állapotgépe (hardverfüggetlen, PC-n tesztelhető).
//
// Mit csinál: eldönti, mikor aludjon el az egység (a busz és az ECU hosszabb ideig csendes), és
//             az alvás közbeni rövid figyelőablakok eredményéből, mikor ébredjen fel.
// Hogyan:     az "aktivitás" (CAN-keret vagy élő lekérdezési ciklus) időbélyegét tárolja; ha a
//             legutóbbi aktivitás óta eltelt az üresjárati idő, alvásra kész. Alvás alatt az
//             időzítős ébredések után figyelőablak jön: ha az ablakban a buszon volt jel, az
//             egység felébred, különben visszaalszik.
// Miért:      az SN65HVD231 alvó módban a vevő is kikapcsol (H-11), ezért busz-forgalomra nem
//             ébreszt; az ébredéshez időzítős, rövid figyelőablak kell (V-12). A döntési
//             logika külön áll, hogy a hardver nélkül is tesztelhető legyen.
#include <cstdint>

namespace og {

struct PowerConfig {
    uint32_t idle_to_sleep_ms = 10000;  // ennyi csend után alszik el
    uint32_t sleep_period_ms = 1000;    // az alvási szakaszok hossza (az ébredési késleltetés felső korlátja)
    uint32_t listen_ms = 60;            // a figyelőablak hossza (a keretek ennél sűrűbben követik egymást)
};

enum class PowerState : uint8_t { Active, Sleeping };

class PowerFsm {
public:
    explicit PowerFsm(const PowerConfig& cfg = PowerConfig()) : cfg_(cfg) {}

    // Mit csinál: aktivitást jelez (a hívó időbélyeggel). Ébren tartja az egységet.
    void on_activity(uint32_t now_ms) { last_activity_ms_ = now_ms; }

    // Mit csinál: igaz, ha az egység ébren van, és az utolsó aktivitás óta letelt az üresjárati idő.
    bool should_sleep(uint32_t now_ms) const {
        return state_ == PowerState::Active && now_ms - last_activity_ms_ >= cfg_.idle_to_sleep_ms;
    }

    // Mit csinál: átvált alvó állapotba (a ciklusszámlálót nullázza).
    void enter_sleep() {
        state_ = PowerState::Sleeping;
        windows_ = 0;
    }

    // Mit csinál: egy figyelőablak eredményét dolgozza fel. Igaz, ha az egység felébred.
    bool on_window(bool bus_activity, uint32_t now_ms) {
        if (state_ != PowerState::Sleeping) return false;
        ++windows_;
        if (!bus_activity) return false;
        state_ = PowerState::Active;
        last_activity_ms_ = now_ms;
        return true;
    }

    PowerState state() const { return state_; }
    uint32_t windows() const { return windows_; }  // az utolsó alvás ablakainak száma
    const PowerConfig& config() const { return cfg_; }

private:
    PowerConfig cfg_;
    PowerState state_ = PowerState::Active;
    uint32_t last_activity_ms_ = 0;
    uint32_t windows_ = 0;
};

}  // namespace og
