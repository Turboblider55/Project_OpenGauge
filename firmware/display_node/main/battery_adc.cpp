// Az akkumulátor feszültségének mérése.
//
// Mit csinál: a BTR+ 100 kΩ / 100 kΩ-os osztóval leosztott feszültségét méri az ADC1-gyel.
// Hogyan:     12 dB-es csillapítás (kb. 3,1 V-ig mér; az osztó után legfeljebb 2,1 V jut a lábra),
//             gyári kalibráció görbeillesztéssel, 64 minta átlaga. Az osztásarányt a menuconfig
//             adja meg ezrelékben, így az ellenállások tűrése multiméteres méréssel korrigálható.
// Miért:      az ADC2 a Wi-Fi mellett nem használható, az IO7 viszont az ADC1-en van; a kalibráció
//             nélküli nyers érték chipenként több tíz mV-tal eltérhet.
#include "battery_adc.h"

#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace og {

namespace {
const char* TAG = "battery";
constexpr adc_atten_t kAtten = ADC_ATTEN_DB_12;
constexpr int kSamples = 64;
}  // namespace

bool BatteryAdc::init(int gpio) {
    if (gpio < 0) return false;
    adc_unit_t unit;
    esp_err_t e = adc_oneshot_io_to_channel(gpio, &unit, &channel_);
    if (e != ESP_OK || unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "Az IO%d nem ADC1-láb", gpio);
        return false;
    }
    adc_oneshot_unit_init_cfg_t ucfg = {};
    ucfg.unit_id = unit;
    e = adc_oneshot_new_unit(&ucfg, &unit_);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(e));
        return false;
    }
    adc_oneshot_chan_cfg_t ccfg = {};
    ccfg.atten = kAtten;
    ccfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    e = adc_oneshot_config_channel(unit_, channel_, &ccfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(e));
        return false;
    }
    adc_cali_curve_fitting_config_t cal = {};
    cal.unit_id = unit;
    cal.chan = channel_;
    cal.atten = kAtten;
    cal.bitwidth = ADC_BITWIDTH_DEFAULT;
    e = adc_cali_create_scheme_curve_fitting(&cal, &cali_);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ADC-kalibráció: %s", esp_err_to_name(e));
        return false;
    }
    return true;
}

bool BatteryAdc::read_mv(uint16_t& mv) {
    if (!unit_ || !cali_) return false;
    int32_t sum = 0;
    for (int i = 0; i < kSamples; ++i) {
        int raw = 0;
        if (adc_oneshot_read(unit_, channel_, &raw) != ESP_OK) return false;
        int v = 0;
        if (adc_cali_raw_to_voltage(cali_, raw, &v) != ESP_OK) return false;
        sum += v;
    }
    mv = static_cast<uint16_t>(sum / kSamples * CONFIG_OG_BAT_SCALE_PERMILLE / 1000);
    return true;
}

}  // namespace og
