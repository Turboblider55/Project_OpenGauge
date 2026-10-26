#pragma once
// Első LVGL-teszt a kerek kijelzőhöz (részletek: lvgl_test.cpp).
#include "lcd_panel.h"

namespace og {

// Mit csinál: az LVGL-t a panelre köti, felépíti a tesztképernyőt, és másodpercenként naplózza
//             az FPS-t (nem tér vissza). swap_bytes: RGB565 bájtcsere a panel felé.
void run_lvgl_test(LcdPanel& lcd, bool swap_bytes);

}  // namespace og
