// A beállítások NVS-tárolása: egyetlen blob a "og_cfg" névtérben.
#include "settings_store.h"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"

namespace og {

namespace {
constexpr const char* TAG = "settings";
constexpr const char* kNamespace = "og_cfg";
constexpr const char* kKey = "settings";
}  // namespace

// Mit csinál: beolvassa a blobot az NVS-ből, és a settings_decode() ellenőrzi (hossz, verzió, összeg).
// Miért:      sérült vagy más verziójú adatból nem szabad hibás beállítást használni; ilyenkor alapérték van.
bool settings_load(Settings& out) {
    out = Settings{};
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;  // első indítás: még nincs névtér
    uint8_t buf[kSettingsBlobSize];
    size_t len = sizeof buf;
    const esp_err_t e = nvs_get_blob(h, kKey, buf, &len);
    nvs_close(h);
    if (e != ESP_OK) return false;
    if (!settings_decode(buf, len, out)) {
        ESP_LOGW(TAG, "A mentett beállítás érvénytelen, alapértékek");
        return false;
    }
    return true;
}

// Mit csinál: a beállításokat érvényesíti, bájtsorrá alakítja és az NVS-be írja (commit-tal).
bool settings_save(Settings s) {
    sanitize(s);
    uint8_t buf[kSettingsBlobSize];
    const size_t n = settings_encode(s, buf, sizeof buf);
    nvs_handle_t h;
    if (n == 0 || nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, kKey, buf, n);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGE(TAG, "Mentés sikertelen: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

// Mit csinál: az utolsó VIN-t a beállításoktól külön kulcson tárolja, és csak változáskor ír (flash-kímélés).
bool last_vin_save(const char vin[18]) {
    char old[18];
    if (last_vin_load(old) && std::strncmp(old, vin, 17) == 0) return true;
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, "vin", vin, 17);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK;
}

bool last_vin_load(char out[18]) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 17;
    const esp_err_t e = nvs_get_blob(h, "vin", out, &len);
    nvs_close(h);
    if (e != ESP_OK || len != 17) return false;
    out[17] = 0;
    return true;
}

}  // namespace og
