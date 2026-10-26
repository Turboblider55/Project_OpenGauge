#pragma once
// A kijelzőmodul kapacitív érintővezérlője (CST816S, I2C).
//
// Mit csinál: felállítja az I2C-buszt, végigkeresi a buszon lévő eszközöket (naplózza őket),
//             létrehozza az esp_lcd_touch CST816S-meghajtót, és kikapcsolja a chip automatikus
//             alvását.
// Hogyan:     ESP-IDF i2c_master (új I2C-driver) + esp_lcd_panel_io_i2c + esp_lcd_touch_cst816s;
//             a koordináták tengelycseréje és tükrözése a meghajtóban állítható.
// Miért:      a CST816S néhány másodperc tétlenség után elalszik, és ilyenkor nem válaszol az
//             I2C-kérésekre (a lekérdezés hibákkal telítené a naplót); a buszkeresés pedig az
//             első indításkor megmutatja a címet és azt, hogy a busz egyáltalán működik-e.
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
};

class TouchPanel {
public:
    // Mit csinál: busz, keresés, meghajtó és alvás-tiltás. Hamis, ha a CST816S nem érhető el.
    bool init(const TouchConfig& cfg);

    esp_lcd_touch_handle_t handle() const { return tp_; }

private:
    void scan_bus();

    void* bus_ = nullptr;  // i2c_master_bus_handle_t (az I2C-fejléc ne kerüljön a publikus fejlécbe)
    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_touch_handle_t tp_ = nullptr;
};

}  // namespace og
