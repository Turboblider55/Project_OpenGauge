#pragma once
// Műszerfal a valós telemetriával (részletek: dash_app.cpp).
#include "lcd_panel.h"
#include "status_led.h"
#include "touch_panel.h"

namespace og {

// Mit csinál: elindítja a rádiót, az LVGL-t és a kapcsolatfeladatot, felépíti a három
//             képernyőt (visszatér; a munka az LVGL- és a kapcsolatfeladatban folyik).
//             touch = nullptr esetén érintés nélkül, csak automatikus váltással fut.
//             A led a kapcsolat és az akkumulátor állapotát kapja.
void run_dash_app(LcdPanel& lcd, bool swap_bytes, TouchPanel* touch, StatusLed& led);

}  // namespace og
