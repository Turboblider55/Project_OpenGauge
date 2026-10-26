#pragma once
// Érintésteszt (részletek: touch_test.cpp).
#include "lcd_panel.h"
#include "touch_panel.h"

namespace og {

// Mit csinál: elindítja az LVGL-t az érintővel, és felépíti a tesztképernyőt (visszatér).
void run_touch_test(LcdPanel& lcd, bool swap_bytes, TouchPanel& touch);

}  // namespace og
