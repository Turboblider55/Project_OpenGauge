#pragma once
// Az állapotjelző LED-ek meghajtója (ESP-IDF-függő illesztő).
//
// Mit csinál: saját feladatban 20 ms-onként kiszámolja a LED-ek állapotát (led_pattern.h), és a
//             LEDC-kimenetekre írja. A többi feladat csak az állapotot adja meg (set_...).
// Hogyan:     az állapot atomi változókban él, így bármelyik feladatból hívható, zár nélkül.
//             Változáskor egy naplósort ír, így a jelzés a soros naplóból is követhető.
//             A LEDC 1. időzítőjét és 1-3. csatornáját használja (a 0. a háttérvilágításé).
// Miért:      a minták ideje nem függhet a CAN- vagy a rádióforgalomtól. A PWM a színek eltérő
//             fényerejét egyenlíti ki, és nem kell hozzá külön ki-be kapcsolás a mintákban.
//             Alvás és időzített ébredés alatt a LED-ek nem világítanak (a mintákat nem futtatja).
#include <atomic>
#include <cstdint>

#include "led_pattern.h"

namespace og {

// Lábkiosztás és fényerő hardverváltozatonként; a -1 azt jelenti, hogy nincs LED.
struct LedPins {
    int red, green, blue;
    uint8_t red_pct, green_pct, blue_pct;
};

class StatusLed {
public:
    // Mit csinál: beállítja a LEDC-kimeneteket, és elindítja a LED-feladatot (az önteszttel).
    //             Az előző futás rendellenes végét (esp_reset_reason) Fault::Reset hibaként jelzi.
    // Miért:      a lehető legkorábban kell hívni: így a későbbi indítási hibák is látszanak.
    bool start(const LedPins& pins);

    void set_link(LinkLed link) { link_.store(static_cast<uint8_t>(link), std::memory_order_relaxed); }
    void set_searching(bool on) { searching_.store(on, std::memory_order_relaxed); }

    // Mit csinál: egy hiba be- vagy kikapcsolása (a szűrés a hívó dolga: PersistFilter).
    void set_fault(Fault f, bool on) {
        if (on) faults_.fetch_or(fault_bit(f), std::memory_order_relaxed);
        else faults_.fetch_and(static_cast<uint8_t>(~fault_bit(f)), std::memory_order_relaxed);
    }

private:
    static void task_entry(void* self);
    void task();
    void write(int index, bool on);

    LedPins pins_ = {-1, -1, -1, 0, 0, 0};
    std::atomic<uint8_t> link_{static_cast<uint8_t>(LinkLed::Off)};
    std::atomic<bool> searching_{false};
    std::atomic<uint8_t> faults_{0};
    uint32_t reset_fault_until_ms_ = 0;
};

}  // namespace og
