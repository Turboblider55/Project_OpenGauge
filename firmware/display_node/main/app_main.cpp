// Kijelzőegység – M0 váz: csak azt ellenőrzi, hogy a közös komponensek lefordulnak és a
// protokoll-méretek a vártak. A tényleges funkciók a későbbi mérföldköveknél jönnek.
#include "esp_log.h"
#include "og_codec.h"
#include "pid_table.h"

static const char* TAG = "og_disp";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "OpenGauge Kijelzőegység indul; protokoll v%d, Telemetry = %u B",
             og::kProtoVersion, (unsigned)sizeof(og::TelemetryMsg));
}
