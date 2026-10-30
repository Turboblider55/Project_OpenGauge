#pragma once
// Az állapotjelző LED-ek mintái (hardverfüggetlen, PC-n tesztelhető).
//
// Mit csinál: az eszköz állapotából és az indulás óta eltelt időből kiszámolja, hogy az adott
//             pillanatban melyik LED világítson. A tartós hibák szűrését a PersistFilter végzi.
// Hogyan:     led_frame() tiszta függvény: nincs belső állapota, a minta csak az időből adódik.
//             A meghajtó (og_led) 20 ms-onként hívja, és a LEDC-kimenetekre írja az eredményt.
// Miért:      a szabályok (mi mit jelent, melyik hiba látszik) így hardver nélkül tesztelhetők,
//             és mindkét NYÁK ugyanazt a jelzésrendszert használja. A táblázat a hardver-
//             architektúra dokumentum "Állapotjelző LED-ek" szakaszában található.
#include <cstdint>

namespace og {

// A piros LED hibakódjai: az érték a villanások száma egy sorozatban.
enum class Fault : uint8_t {
    None = 0,
    Ecu = 1,      // az ECU tartósan nem válaszol
    Can = 2,      // CAN-buszhiba (bus-off)
    Radio = 3,    // a másik egység tartósan nem hallható
    Init = 4,     // inicializálási hiba (rádió, kijelző, érintő, CAN-driver)
    Power = 5,    // alacsony tápfeszültség (lemerülő akkumulátor)
    Reset = 6,    // az előző futás rendellenesen ért véget (összeomlás, watchdog, tápesés)
};

// A hibák bitmaszkja: Fault N a (N-1). bit.
inline constexpr uint8_t fault_bit(Fault f) {
    return f == Fault::None ? 0 : static_cast<uint8_t>(1u << (static_cast<uint8_t>(f) - 1));
}

// Mit csinál: a maszkból kiválasztja a legsúlyosabb hibát (ez látszik a piros LED-en).
// Hogyan:     rögzített fontossági sorrend: ami a működést megakadályozza, az áll elöl.
inline Fault top_fault(uint8_t mask) {
    constexpr Fault kOrder[] = {Fault::Init, Fault::Can, Fault::Power, Fault::Ecu, Fault::Radio, Fault::Reset};
    for (Fault f : kOrder)
        if (mask & fault_bit(f)) return f;
    return Fault::None;
}

// Súlyos hiba: a fő funkció nem működik, ezért a zöld LED is villog.
inline bool fault_is_severe(Fault f) { return f == Fault::Init || f == Fault::Can; }

// A kék LED állapota.
enum class LinkLed : uint8_t {
    Off,      // a rádió még nem indult el (vagy nem indult el)
    Waiting,  // a rádió fut, de a másik egység nem hallható
    Ok,       // friss csomag jön a másik egységtől
};

struct LedState {
    bool searching = false;        // protokollkeresés (csak az OBD-egységen)
    LinkLed link = LinkLed::Off;
    uint8_t faults = 0;            // fault_bit() értékek VAGY-kapcsolata
};

struct LedOutput {
    bool red, green, blue;
    bool operator==(const LedOutput& o) const { return red == o.red && green == o.green && blue == o.blue; }
};

constexpr uint32_t kLedSelfTestStepMs = 300;   // induláskor színenként ennyi ideig világít
constexpr uint32_t kLedSelfTestMs = 3 * kLedSelfTestStepMs;
constexpr uint32_t kLedAlivePeriodMs = 2000;   // zöld: folyamatos fény, ennyi ms-onként egy kihagyás
constexpr uint32_t kLedAliveGapMs = 100;
constexpr uint32_t kLedSearchPeriodMs = 2000;  // zöld: protokollkeresés (lassú villogás)
constexpr uint32_t kLedSeverePeriodMs = 1000;  // zöld: súlyos hiba (1 Hz)
constexpr uint32_t kLedWaitPeriodMs = 500;     // kék: nincs friss adat (2 Hz)
constexpr uint32_t kLedCodeStepMs = 200;       // piros: egy villanás és az utána lévő szünet hossza
constexpr uint32_t kLedCodePauseMs = 1500;     // piros: szünet a sorozatok között

// Mit csinál: megmondja, hogy a piros LED világít-e a hibakód sorozatának adott pillanatában.
// Hogyan:     N villanás (be + ki), majd hosszú szünet; a sorozat periodikus.
inline bool red_code_on(Fault f, uint32_t t_ms) {
    const uint32_t n = static_cast<uint32_t>(f);
    if (n == 0) return false;
    const uint32_t flashes_ms = 2 * n * kLedCodeStepMs;
    const uint32_t t = t_ms % (flashes_ms + kLedCodePauseMs);
    return t < flashes_ms && (t / kLedCodeStepMs) % 2 == 0;
}

// Mit csinál: a LED-ek pillanatnyi állapota. t_ms: a meghajtó indulása óta eltelt idő.
// Hogyan:     induláskor piros-zöld-kék önteszt; utána a három LED egymástól függetlenül.
// Miért:      az önteszt mindhárom LED-et és a bekötést is megmutatja; a zöld "kihagyása" azt
//             jelzi, hogy a program fut (a folyamatos fény egy lefagyott rendszeren is megmaradna).
inline LedOutput led_frame(const LedState& s, uint32_t t_ms) {
    if (t_ms < kLedSelfTestMs) {
        const uint32_t step = t_ms / kLedSelfTestStepMs;
        return LedOutput{step == 0, step == 1, step == 2};
    }
    const Fault top = top_fault(s.faults);
    LedOutput o{};
    o.red = red_code_on(top, t_ms);

    if (fault_is_severe(top))
        o.green = (t_ms % kLedSeverePeriodMs) < kLedSeverePeriodMs / 2;
    else if (s.searching)
        o.green = (t_ms % kLedSearchPeriodMs) < kLedSearchPeriodMs / 2;
    else
        o.green = (t_ms % kLedAlivePeriodMs) >= kLedAliveGapMs;

    switch (s.link) {
        case LinkLed::Off: o.blue = false; break;
        case LinkLed::Waiting: o.blue = (t_ms % kLedWaitPeriodMs) < kLedWaitPeriodMs / 2; break;
        case LinkLed::Ok: o.blue = true; break;
    }
    return o;
}

// Tartós állapot szűrője: az eseti kimaradásokból nem lesz hiba.
//
// Mit csinál: a kimenet akkor lesz igaz, ha a bemenet legalább on_ms ideje folyamatosan igaz,
//             és akkor lesz hamis, ha legalább off_ms ideje folyamatosan hamis.
// Miért:      egy-egy elveszett csomag vagy ECU-válasz nem jelenhet meg hibaként, és a jelzés
//             sem szabad, hogy a határérték körül villódzzon.
class PersistFilter {
public:
    PersistFilter(uint32_t on_ms, uint32_t off_ms) : on_ms_(on_ms), off_ms_(off_ms) {}

    bool update(bool input, uint32_t now_ms) {
        if (input == output_) {
            pending_ = false;
            return output_;
        }
        if (!pending_) {
            pending_ = true;
            since_ms_ = now_ms;
        }
        if (now_ms - since_ms_ >= (input ? on_ms_ : off_ms_)) {
            output_ = input;
            pending_ = false;
        }
        return output_;
    }

    bool output() const { return output_; }

private:
    uint32_t on_ms_, off_ms_;
    uint32_t since_ms_ = 0;
    bool pending_ = false;
    bool output_ = false;
};

}  // namespace og
