// A beállítások tartós tárolása az NVS-ben (a bájtformátum a disp_model/settings.h-ban van).
#pragma once

#include "gear_model.h"
#include "settings.h"

namespace og {

// Betölti a mentett beállításokat; ha nincs vagy sérült, alapértékeket ad (és false-t).
bool settings_load(Settings& out);

// Elmenti a beállításokat (előtte érvényesíti őket). Siker esetén true.
bool settings_save(Settings s);

// Az utoljára kapott VIN mentése / betöltése (a következő indítás logójához). A betöltés false-t ad, ha nincs mentett.
bool last_vin_load(char out[18]);
bool last_vin_save(const char vin[18]);

// A tanult fokozat-áttételek mentése / betöltése (külön kulcson, a beállításoktól függetlenül). A betöltés false-t ad,
// ha nincs mentett vagy sérült adat (a modell ilyenkor változatlan).
bool gear_state_load(GearModel& m);
bool gear_state_save(const GearModel& m);

}  // namespace og
