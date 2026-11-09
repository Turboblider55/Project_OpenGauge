// A beállítások képernyője: görgethető lista kapcsolókkal és léptető gombokkal.
#pragma once

#include "lvgl.h"
#include "settings.h"

namespace og {

struct UiColors {
    lv_color_t bg, track, text, dim, accent, warn;
};

class SettingsView {
public:
    using ExitFn = void (*)(const Settings& edited);

    // A képernyő csak megnyitás és az azt követő törlés között létezik (nullptr, ha nincs felépítve).
    lv_obj_t* scr = nullptr;

    // Elmenti a színeket és a kilépési visszahívást; a képernyőt még nem építi fel (az LVGL-kupacot nem terheli).
    // Kilépéskor (VISSZA gomb vagy jobbra húzás) hívja on_exit-et.
    void init(const UiColors& colors, ExitFn on_exit);

    // Felépíti a képernyőt (az LVGL zárja alatt), és betölti a jelenlegi beállításokat a vezérlőkbe.
    // Megnyitás előtt kell hívni; utána a scr betölthető. Az LVGL az átlépés után törölheti (auto_del), a scr ilyenkor nullázódik.
    void open(const Settings& current);

private:
    friend struct SettingsViewImpl;
    Settings edit_;
    Settings orig_;  // a megnyitáskori érték: ehhez képest látszik, van-e módosítás
    lv_obj_t* back_label_ = nullptr;  // a VISSZA / ALKALMAZ gomb felirata
    ExitFn on_exit_ = nullptr;
    lv_obj_t* list_ = nullptr;

    void build();  // felépíti a képernyő objektumait
};

}  // namespace og
