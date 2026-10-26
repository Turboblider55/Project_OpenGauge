#pragma once
// A kijelzőmodul kapacitív érintővezérlője (CST816S, I2C).
//
// Mit csinál: felállítja az I2C-buszt, reseteli a chipet, kikapcsolja az automatikus alvását,
//             végigkeresi a buszt (naplózza az eszközöket), és létrehozza az esp_lcd_touch
//             CST816S-meghajtót (a CST816T-vel regiszterkompatibilis).
// Hogyan:     ESP-IDF i2c_master (új I2C-driver) + esp_lcd_panel_io_i2c + esp_lcd_touch_cst816s;
//             a koordináták tengelycseréje és tükrözése a meghajtóban állítható.
// Miért:      a chip a reset után kb. 100-200 ms-mal elalszik, és ilyenkor nem válaszol az
//             I2C-kérésekre (mérés a devkiten); a buszkeresés az első indításkor megmutatja a
//             címet és azt, hogy a busz egyáltalán működik-e.
#include <cstdint>

#include "esp_lcd_touch.h"

namespace og {

struct TouchPins {
    int sda;
    int scl;
    int rst;   // -1 = nincs bekötve
    int intr;  // -1 = nincs bekötve
};

struct TouchConfig {
    TouchPins pins;
    uint16_t x_max = 360;
    uint16_t y_max = 360;
    bool swap_xy = false;
    bool mirror_x = false;
    bool mirror_y = false;
    // Igaz: az LVGL csak a chip megszakítására olvas (eseményvezérelt); hamis: időzített
    // lekérdezés. A CST816 megszakításai ritkák, ezért eseményvezérelt módban az LVGL kevés
    // mintát kap egy húzásról, és a gesztusok egy része elveszhet.
    bool use_interrupt = false;
};

class TouchPanel {
public:
    // Mit csinál: busz, keresés, meghajtó és alvás-tiltás. Hamis, ha a CST816S nem érhető el.
    bool init(const TouchConfig& cfg);

    esp_lcd_touch_handle_t handle() const { return tp_; }

    // Igaz, ha a chip a címére nyugtáz (ébren van). Diagnosztikához.
    bool responds() const;

private:
    void scan_bus();
    void disable_auto_sleep();

    void* bus_ = nullptr;  // i2c_master_bus_handle_t (az I2C-fejléc ne kerüljön a publikus fejlécbe)
    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_touch_handle_t tp_ = nullptr;
};

}  // namespace og
