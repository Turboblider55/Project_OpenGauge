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
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

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

// Perifériaoldal: a GATT-szolgáltatás és a kapcsolat állapota.
const char* g_periph_name = nullptr;
uint16_t g_val_handle = 0;
std::atomic<uint16_t> g_conn{BLE_HS_CONN_HANDLE_NONE};
std::atomic<bool> g_subscribed{false};
std::atomic<uint32_t> g_connects{0};
std::atomic<uint32_t> g_disconnects{0};
std::atomic<int> g_last_reason{0};
std::atomic<uint32_t> g_itvl_us{0};
std::atomic<uint32_t> g_notify_ok{0};
std::atomic<uint32_t> g_notify_fail{0};
std::atomic<uint32_t> g_counter{0};

// Saját (teszt) 128 bites azonosítók.
const ble_uuid128_t kSvcUuid = BLE_UUID128_INIT(0x4f, 0x47, 0x00, 0x01, 0x6f, 0x70, 0x65, 0x6e, 0x67, 0x61, 0x75, 0x67, 0x65, 0x00, 0x00, 0x01);
const ble_uuid128_t kChrUuid = BLE_UUID128_INIT(0x4f, 0x47, 0x00, 0x02, 0x6f, 0x70, 0x65, 0x6e, 0x67, 0x61, 0x75, 0x67, 0x65, 0x00, 0x00, 0x01);
ble_gatt_chr_def g_chrs[2];
ble_gatt_svc_def g_svcs[2];

void periph_advertise();
void gatt_init();

// Mit csinál: a host szinkronjának jelzése (a verem ettől fogva használható).
void on_sync() {
    ble_hs_id_infer_auto(0, &g_own_addr_type);
    g_synced = true;
}

void host_task(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Mit csinál: a jellemző olvasása (a számláló jelenlegi értéke).
int chr_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    const uint32_t v = g_counter;
    return os_mbuf_append(ctxt->om, &v, sizeof v) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// Mit csinál: a kapcsolat eseményeit dolgozza fel (kapcsolódás, bontás, feliratkozás).
int periph_event(ble_gap_event* ev, void*) {
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status == 0) {
                g_conn = ev->connect.conn_handle;
                g_connects++;
                ble_gap_conn_desc d;
                if (ble_gap_conn_find(ev->connect.conn_handle, &d) == 0) {
                    g_itvl_us = d.conn_itvl * 1250u;
                    ESP_LOGI(TAG, "Kapcsolódott: periódus %lu us, késleltetés %d, időtúllépés %d ms",
                             static_cast<unsigned long>(g_itvl_us.load()), d.conn_latency, d.supervision_timeout * 10);
                }
            } else {
                periph_advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            g_conn = BLE_HS_CONN_HANDLE_NONE;
            g_subscribed = false;
            g_disconnects++;
            g_last_reason = ev->disconnect.reason;
            ESP_LOGW(TAG, "Bontva, ok: %d", ev->disconnect.reason);
            periph_advertise();
            break;
        case BLE_GAP_EVENT_CONN_UPDATE: {
            ble_gap_conn_desc d;
            if (ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0) {
                g_itvl_us = d.conn_itvl * 1250u;
                ESP_LOGI(TAG, "Kapcsolat-paraméterek: periódus %lu us, késleltetés %d, időtúllépés %d ms",
                         static_cast<unsigned long>(g_itvl_us.load()), d.conn_latency, d.supervision_timeout * 10);
            }
            break;
        }
        case BLE_GAP_EVENT_SUBSCRIBE:
            g_subscribed = ev->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "Értesítés: %s", g_subscribed ? "feliratkozva" : "leiratkozva");
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            periph_advertise();
            break;
        default:
            break;
    }
    return 0;
}

// Mit csinál: csatlakoztatható hirdetést indít a perifériaként megadott névvel.
void periph_advertise() {
    ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = reinterpret_cast<const uint8_t*>(g_periph_name);
    f.name_len = static_cast<uint8_t>(strlen(g_periph_name));
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGE(TAG, "periph adv_set_fields: %d", rc);
        return;
    }
    ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = p.itvl_max = 160;  // 100 ms
    rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &p, periph_event, nullptr);
    if (rc != 0) ESP_LOGE(TAG, "periph adv_start: %d", rc);
}

// Mit csinál: a GATT-táblát (egy szolgáltatás, egy olvasható és értesíthető jellemző) felveszi.
void gatt_init() {
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(g_periph_name);
    memset(g_chrs, 0, sizeof g_chrs);
    g_chrs[0].uuid = &kChrUuid.u;
    g_chrs[0].access_cb = chr_access;
    g_chrs[0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
    g_chrs[0].val_handle = &g_val_handle;
    memset(g_svcs, 0, sizeof g_svcs);
    g_svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    g_svcs[0].uuid = &kSvcUuid.u;
    g_svcs[0].characteristics = g_chrs;
    ble_gatts_count_cfg(g_svcs);
    ble_gatts_add_svcs(g_svcs);
}

// Mit csinál: másodpercenként növeli a számlálót, és ha van feliratkozott kliens, értesítést küld.
void notify_task(void*) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const uint32_t v = ++g_counter;
        const uint16_t conn = g_conn;
        if (conn == BLE_HS_CONN_HANDLE_NONE || !g_subscribed) continue;
        os_mbuf* om = ble_hs_mbuf_from_flat(&v, sizeof v);
        if (om && ble_gatts_notify_custom(conn, g_val_handle, om) == 0) g_notify_ok++;
        else g_notify_fail++;
    }
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
    if (g_periph_name) gatt_init();
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

bool ble_start_peripheral(const char* name) {
    g_periph_name = name;
    if (!stack_start()) return false;
    periph_advertise();
    xTaskCreate(notify_task, "ble_notify", 3072, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "Csatlakoztatható hirdetés indul: \"%s\"", name);
    return true;
}

BleConnStats ble_conn_stats() {
    BleConnStats s;
    s.connected = g_conn != BLE_HS_CONN_HANDLE_NONE;
    s.subscribed = g_subscribed;
    s.connects = g_connects;
    s.disconnects = g_disconnects;
    s.last_reason = g_last_reason;
    s.itvl_us = g_itvl_us;
    s.notify_ok = g_notify_ok;
    s.notify_fail = g_notify_fail;
    s.counter = g_counter;
    return s;
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
