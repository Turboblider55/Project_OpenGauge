#pragma once
// Az LVGL elindítása a kerek kijelzőn (közös az LVGL-teszthez és a műszerfalhoz).
#include <cstdint>

#include "lcd_panel.h"
#include "lvgl.h"
#include "touch_panel.h"

namespace og {

// Mit csinál: elindítja az esp_lvgl_port-ot (LVGL-feladat az 1. magon) és a panelre köti az
//             LVGL-t részleges frissítéssel, két 40 soros DMA-pufferrel; bekapcsolja a
//             képkockaszámlálót. nullptr, ha nem sikerült.
// Miért:      a 0. mag a rádióé (Wi-Fi, ESP-NOW), az 1. mag a rajzolásé, így egymást nem lassítják.
lv_display_t* lvgl_start(LcdPanel& lcd, bool swap_bytes);

// Mit csinál: az érintőt LVGL-bemenetként a kijelzőhöz köti (gesztusok, nyomás, húzás).
lv_indev_t* lvgl_add_touch(lv_display_t* disp, TouchPanel& touch);

// Az utolsó hívás óta kirajzolt képkockák száma (a számláló nullázódik).
uint32_t lvgl_take_frames();

}  // namespace og
