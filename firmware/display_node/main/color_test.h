#pragma once
// Színteszt a kerek kijelzőhöz (részletek: color_test.cpp).
#include "lcd_panel.h"

namespace og {

// Mit csinál: végtelen ciklusban tesztképeket rajzol az inicializált panelre (nem tér vissza).
void run_color_test(LcdPanel& lcd);

}  // namespace og
