#pragma once
// Az akkumulátor feszültségének mérése az ADC-vel (ESP-IDF-függő, részletek: battery_adc.cpp).
#include <cstdint>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"

namespace og {

class BatteryAdc {
public:
    // Mit csinál: előkészíti az ADC1 csatornáját a megadott lábon. Hamis, ha nincs ilyen láb.
    bool init(int gpio);

    // Mit csinál: megméri a cella feszültségét (mV); több minta átlaga, az osztóval visszaszorozva.
    bool read_mv(uint16_t& mv);

private:
    adc_oneshot_unit_handle_t unit_ = nullptr;
    adc_cali_handle_t cali_ = nullptr;
    adc_channel_t channel_ = ADC_CHANNEL_0;
};

}  // namespace og
