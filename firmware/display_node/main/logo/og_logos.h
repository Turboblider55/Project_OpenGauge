// Az indító logók: márkához tartozó kép keresése.
#pragma once

#include "lvgl.h"

namespace og {

// Mit csinál: a VIN-ből felismert márka (og_vin.h azonosító) logóját adja; nullptr, ha ehhez a márkához nincs logó.
// Miért:      ha nincs megfelelő logó, az indító logó lépés teljesen kimarad.
const lv_image_dsc_t* logo_for_brand(int brand);

}  // namespace og
