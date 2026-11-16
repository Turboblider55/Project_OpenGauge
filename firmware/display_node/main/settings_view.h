// A beállítások képernyői: a fő lista (kapcsolók, léptetők, váltógombok) és két al-képernyő: fokozatbeállítások és képernyősorrend.
//
// Mit csinál: a fő lista a beállításokat soronként mutatja; két gombja a fokozatbeállítások és a képernyők sorrendjének
//             külön képernyőjére visz. Minden képernyő ugyanazt a szerkesztett másolatot módosítja; az al-képernyők gombja
//             (VISSZA / MENTÉS) a fő listára lép vissza, a módosítások ilyenkor még nincsenek véglegesítve: csak a fő lista
//             ALKALMAZ gombja menti és érvényesíti őket, a fő lista jobbra húzása pedig mindent elvet.
// Hogyan:     mindhárom képernyő csak megnyitáskor épül fel, és az elhagyáskor az LVGL törli (auto_del), így nem foglalnak
//             állandóan memóriát. Az al-képernyők jobbra húzása a VISSZA gombbal egyenértékű (a módosítások megmaradnak).
#pragma once

#include "lvgl.h"
#include "settings.h"

namespace og {

struct UiColors {
    lv_color_t bg, track, text, dim, accent, warn;
};

class SettingsView {
public:
    using ExitFn = void (*)(const Settings& edited);  // ALKALMAZ / VISSZA a fő listán: a módosítás (vagy változatlan érték) végleges
    using CancelFn = void (*)();                      // jobbra húzás a fő listán: a módosítások elvetése

    // A fő lista képernyője; csak megnyitás és az azt követő törlés között létezik (nullptr, ha nincs felépítve).
    lv_obj_t* scr = nullptr;

    // Elmenti a színeket és a visszahívásokat; a képernyőt még nem építi fel (az LVGL memóriáját nem terheli).
    void init(const UiColors& colors, ExitFn on_exit, CancelFn on_cancel);

    // Felépíti a fő lista képernyőjét (az LVGL zárja alatt), és betölti a jelenlegi beállításokat a vezérlőkbe.
    // Megnyitás előtt kell hívni; utána a scr betölthető. Az LVGL az átlépés után törölheti (auto_del), a scr ilyenkor nullázódik.
    void open(const Settings& current);

private:
    friend struct SettingsViewImpl;
    Settings edit_;
    Settings orig_;       // a megnyitáskori érték: ehhez képest látszik, van-e módosítás
    Settings page_orig_;  // az aktuális al-képernyő megnyitáskori értéke (a VISSZA / MENTÉS felirathoz)
    lv_obj_t* back_label_ = nullptr;       // a fő lista VISSZA / ALKALMAZ gombjának felirata
    lv_obj_t* sub_back_label_ = nullptr;   // az al-képernyő gombjának felirata
    lv_obj_t* gear_scr_ = nullptr;
    lv_obj_t* order_scr_ = nullptr;
    ExitFn on_exit_ = nullptr;
    CancelFn on_cancel_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* gear_hint_ = nullptr;        // a fokozat-képernyő magyarázó szövege automata módban

    void build();  // felépíti a fő lista képernyőjét
};

}  // namespace og
