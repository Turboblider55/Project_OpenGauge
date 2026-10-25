#pragma once
// Műszerfal a valós telemetriával (részletek: dash_app.cpp).
#include "lcd_panel.h"

namespace og {

// Mit csinál: elindítja a rádiót, az LVGL-t és a kapcsolatfeladatot, felépíti a három
//             képernyőt (visszatér; a munka az LVGL- és a kapcsolatfeladatban folyik).
void run_dash_app(LcdPanel& lcd, bool swap_bytes);

}  // namespace og
