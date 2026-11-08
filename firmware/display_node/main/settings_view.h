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

    lv_obj_t* scr = nullptr;

    // Felépíti a képernyőt (egyszer, az LVGL zárja alatt). Kilépéskor (VISSZA gomb vagy jobbra húzás) hívja on_exit-et.
    void build(const UiColors& colors, ExitFn on_exit);

    // Betölti a jelenlegi beállításokat a vezérlőkbe; megnyitás előtt kell hívni.
    void open(const Settings& current);

private:
    friend struct SettingsViewImpl;
    Settings edit_;
    Settings orig_;  // a megnyitáskori érték: ehhez képest látszik, van-e módosítás
    lv_obj_t* back_label_ = nullptr;  // a VISSZA / ALKALMAZ gomb felirata
    ExitFn on_exit_ = nullptr;
    lv_obj_t* list_ = nullptr;
};

}  // namespace og
