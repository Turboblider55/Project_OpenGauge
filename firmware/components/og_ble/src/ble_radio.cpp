#include "ble_radio.h"

#include <atomic>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

namespace og {

namespace {

const char* TAG = "og_ble";

constexpr uint16_t kCompanyId = 0xFFFF;  // fenntartott (teszt) cégazonosító
constexpr uint8_t kMagic0 = 'O';
constexpr uint8_t kMagic1 = 'G';

std::atomic<bool> g_synced{false};
std::atomic<uint32_t> g_adverts{0};
std::atomic<uint32_t> g_seen{0};
std::atomic<uint32_t> g_lost{0};
std::atomic<int> g_rssi{0};
std::atomic<int32_t> g_last_counter{-1};
uint8_t g_own_addr_type = 0;
BleRole g_role = BleRole::ObdNode;
uint32_t g_itvl_ms = 100;
uint32_t g_window_pct = 100;

// Mit csinál: a host szinkronjának jelzése (a verem ettől fogva használható).
void on_sync() {
    ble_hs_id_infer_auto(0, &g_own_addr_type);
    g_synced = true;
}

void host_task(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Mit csinál: egyszer elindítja az NVS-t és a NimBLE-t, és megvárja a host szinkronját.
bool stack_start() {
    static bool started = false;
    if (started) return true;
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "NVS: %s", esp_err_to_name(e));
        return false;
    }
    e = nimble_port_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(e));
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    for (int i = 0; i < 100 && !g_synced; ++i) vTaskDelay(pdMS_TO_TICKS(50));
    if (!g_synced) {
        ESP_LOGE(TAG, "A BLE host nem szinkronizált");
        return false;
    }
    started = true;
    return true;
}

// Mit csinál: a hirdetést a megadott másodperc-számlálóval (újra)indítja.
// Hogyan:     a gyártói adat csak álló hirdetésnél cserélhető, ezért leállítás, mezők, indítás.
void advertise(uint16_t counter) {
    ble_gap_adv_stop();
    uint8_t mfg[7] = {static_cast<uint8_t>(kCompanyId & 0xFF), static_cast<uint8_t>(kCompanyId >> 8),
                      kMagic0, kMagic1, static_cast<uint8_t>(g_role),
                      static_cast<uint8_t>(counter & 0xFF), static_cast<uint8_t>(counter >> 8)};
    ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.mfg_data = mfg;
    f.mfg_data_len = sizeof mfg;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields: %d", rc);
        return;
    }
    ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_NON;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = p.itvl_max = static_cast<uint16_t>(g_itvl_ms * 1000 / 625);
    rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &p, nullptr, nullptr);
    if (rc != 0) ESP_LOGE(TAG, "adv_start: %d", rc);
}

void adv_task(void*) {
    uint16_t counter = 0;
    for (;;) {
        advertise(counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Mit csinál: egy szkennelt hirdetést dolgoz fel; csak a saját (OG) csomagot számolja.
int on_gap_event(ble_gap_event* ev, void*) {
    if (ev->type != BLE_GAP_EVENT_DISC) return 0;
    ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, ev->disc.data, ev->disc.length_data) != 0) return 0;
    if (f.mfg_data_len != 7 || f.mfg_data[2] != kMagic0 || f.mfg_data[3] != kMagic1) return 0;
    const int32_t counter = f.mfg_data[5] | (f.mfg_data[6] << 8);
    g_adverts++;
    g_rssi = ev->disc.rssi;
    const int32_t last = g_last_counter;
    if (counter != last) {
        g_seen++;
        if (last >= 0 && counter > last + 1) g_lost += static_cast<uint32_t>(counter - last - 1);
        g_last_counter = counter;
    }
    return 0;
}

}  // namespace

bool ble_start_advertising(BleRole role, uint32_t itvl_ms) {
    g_role = role;
    g_itvl_ms = itvl_ms;
    if (!stack_start()) return false;
    xTaskCreate(adv_task, "ble_adv", 3072, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "Hirdetés indul: %lu ms periódus", static_cast<unsigned long>(itvl_ms));
    return true;
}

bool ble_start_scanning(uint32_t window_pct) {
    g_window_pct = window_pct > 100 ? 100 : (window_pct ? window_pct : 1);
    if (!stack_start()) return false;
    ble_gap_disc_params p;
    memset(&p, 0, sizeof p);
    p.passive = 1;
    p.filter_duplicates = 0;
    p.itvl = 160;  // 100 ms
    p.window = static_cast<uint16_t>(160 * g_window_pct / 100);
    const int rc = ble_gap_disc(g_own_addr_type, BLE_HS_FOREVER, &p, on_gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc: %d", rc);
        return false;
    }
    ESP_LOGI(TAG, "Szkennelés indul: ablak %lu %%", static_cast<unsigned long>(g_window_pct));
    return true;
}

BleScanStats ble_scan_stats() {
    BleScanStats s;
    s.adverts = g_adverts;
    s.seconds_seen = g_seen;
    s.seconds_lost = g_lost;
    s.rssi = g_rssi;
    s.last_counter = g_last_counter;
    return s;
}

}  // namespace og
