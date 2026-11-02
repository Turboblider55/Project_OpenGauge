#include "ble_ams.h"

#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "ble_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "os/os_mbuf.h"

namespace og {

namespace {

const char* TAG = "og_ams";

// Az AMS 128 bites azonosítói (az Apple specifikációja szerint, little-endian bájtsorrendben).
const ble_uuid128_t kAmsSvc = BLE_UUID128_INIT(0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E, 0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89);
const ble_uuid128_t kRemoteCmd = BLE_UUID128_INIT(0xC2, 0x51, 0xCA, 0xF7, 0x56, 0x0E, 0xDF, 0xB8, 0x8A, 0x4A, 0xB1, 0x57, 0xD8, 0x81, 0x3C, 0x9B);
const ble_uuid128_t kEntityUpdate = BLE_UUID128_INIT(0x02, 0xC1, 0x96, 0xBA, 0x92, 0xBB, 0x0C, 0x9A, 0x1F, 0x41, 0x8D, 0x80, 0xCE, 0xAB, 0x7C, 0x2F);
const ble_uuid128_t kEntityAttr = BLE_UUID128_INIT(0xD7, 0xD5, 0xBB, 0x70, 0xA8, 0xA3, 0xAB, 0xA6, 0xD8, 0x46, 0xAB, 0x23, 0x8C, 0xF3, 0xB2, 0xC6);

constexpr uint8_t kEntityPlayer = 0, kEntityTrack = 2;

// A kliens lépései: felfedezés, CCCD-írás, feliratkozás az entitásokra.
enum class Step : uint8_t { Idle, DiscSvc, DiscChrs, DiscDscRc, DiscDscEu, SubRc, SubEu, EntPlayer, EntTrack, Ready };

struct Handles {
    uint16_t svc_start = 0, svc_end = 0;
    uint16_t rc_def = 0, rc_val = 0, rc_cccd = 0;
    uint16_t eu_def = 0, eu_val = 0, eu_cccd = 0;
    uint16_t ea_def = 0, ea_val = 0;
};

SemaphoreHandle_t g_mutex = nullptr;
AmsInfo g_info;
Handles g_h;
volatile Step g_step = Step::Idle;
volatile uint16_t g_conn = BLE_HS_CONN_HANDLE_NONE;
const char* g_name = nullptr;
bool g_trunc_busy = false;
uint8_t g_trunc_ent = 0, g_trunc_attr = 0;

void advertise();
void next_step();

// Mit csinál: belép a mutexszel védett szakaszba (az információs struktúra több szálról érhető el).
struct Lock {
    Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(g_mutex); }
};

// Mit csinál: egy (nem nullával lezárt) bájtsort a célpufferbe másol, szükség esetén levágva, és lezárja.
void copy_str(char* dst, size_t cap, const uint8_t* v, size_t n) {
    if (n >= cap) n = cap - 1;
    memcpy(dst, v, n);
    dst[n] = 0;
}

float to_float(const char* s) { return static_cast<float>(strtod(s, nullptr)); }

// Mit csinál: egy entitás-attribútum értékét beírja az információs struktúrába.
// Hogyan:     a Player/PlaybackInfo "állapot,sebesség,eltelt idő" szöveg; a Duration és a Volume szöveges szám.
void apply(uint8_t ent, uint8_t attr, const uint8_t* v, size_t n) {
    char tmp[96];
    copy_str(tmp, sizeof tmp, v, n);
    Lock l;
    if (ent == kEntityPlayer) {
        if (attr == 0) {
            copy_str(g_info.player, sizeof g_info.player, v, n);
        } else if (attr == 1) {
            char* end = nullptr;
            g_info.playback = static_cast<int>(strtol(tmp, &end, 10));
            g_info.playback_stamp_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            if (end && *end == ',') {
                g_info.rate = to_float(end + 1);
                const char* c = strchr(end + 1, ',');
                if (c) g_info.elapsed_s = to_float(c + 1);
            }
        } else if (attr == 2) {
            g_info.volume = to_float(tmp);
        }
    } else if (ent == kEntityTrack) {
        if (attr == 0) copy_str(g_info.artist, sizeof g_info.artist, v, n);
        else if (attr == 1) copy_str(g_info.album, sizeof g_info.album, v, n);
        else if (attr == 2) copy_str(g_info.title, sizeof g_info.title, v, n);
        else if (attr == 3) g_info.duration_s = to_float(tmp);
    }
    g_info.updates++;
}

// Mit csinál: a felfedezés/írás hibáját naplózza, és leállítja a lépéssort.
void fail(const char* what, int status) {
    ESP_LOGE(TAG, "AMS: %s sikertelen (állapot %d; 0x105 = az ATT hitelesítést kér)", what, status);
    g_step = Step::Idle;
}

int svc_cb(uint16_t, const ble_gatt_error* err, const ble_gatt_svc* svc, void*) {
    if (err->status == 0 && svc) {
        g_h.svc_start = svc->start_handle;
        g_h.svc_end = svc->end_handle;
        return 0;
    }
    if (err->status == BLE_HS_EDONE && g_h.svc_start) {
        g_step = Step::DiscChrs;
        next_step();
    } else {
        fail("az AMS-szolgáltatás keresése (az iPhone nem kínálja fel; párosítás kell)", err->status);
    }
    return 0;
}

int chr_cb(uint16_t, const ble_gatt_error* err, const ble_gatt_chr* chr, void*) {
    if (err->status == 0 && chr) {
        if (ble_uuid_cmp(&chr->uuid.u, &kRemoteCmd.u) == 0) {
            g_h.rc_def = chr->def_handle;
            g_h.rc_val = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &kEntityUpdate.u) == 0) {
            g_h.eu_def = chr->def_handle;
            g_h.eu_val = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &kEntityAttr.u) == 0) {
            g_h.ea_def = chr->def_handle;
            g_h.ea_val = chr->val_handle;
        }
        return 0;
    }
    if (err->status == BLE_HS_EDONE && g_h.rc_val && g_h.eu_val) {
        g_step = Step::DiscDscRc;
        next_step();
    } else {
        fail("az AMS-jellemzők keresése", err->status);
    }
    return 0;
}

// Mit csinál: a megadott jellemző leíróinak utolsó fogópontja (a következő jellemző-definíció előtti).
uint16_t dsc_range_end(uint16_t def) {
    uint16_t next = static_cast<uint16_t>(g_h.svc_end + 1);
    for (uint16_t d : {g_h.rc_def, g_h.eu_def, g_h.ea_def}) {
        if (d > def && d < next) next = d;
    }
    return static_cast<uint16_t>(next - 1);
}

int dsc_cb(uint16_t, const ble_gatt_error* err, uint16_t, const ble_gatt_dsc* dsc, void* arg) {
    uint16_t* target = static_cast<uint16_t*>(arg);
    if (err->status == 0 && dsc) {
        if (dsc->uuid.u.type == BLE_UUID_TYPE_16 && dsc->uuid.u16.value == 0x2902) *target = dsc->handle;
        return 0;
    }
    if (err->status == BLE_HS_EDONE && *target) {
        g_step = (g_step == Step::DiscDscRc) ? Step::DiscDscEu : Step::SubRc;
        next_step();
    } else {
        fail("a CCCD keresése", err->status);
    }
    return 0;
}

int write_cb(uint16_t, const ble_gatt_error* err, ble_gatt_attr*, void*) {
    if (err->status != 0) {
        fail("írás", err->status);
        return 0;
    }
    switch (g_step) {
        case Step::SubRc: g_step = Step::SubEu; break;
        case Step::SubEu: g_step = Step::EntPlayer; break;
        case Step::EntPlayer: g_step = Step::EntTrack; break;
        case Step::EntTrack: {
            g_step = Step::Ready;
            Lock l;
            g_info.ready = true;
            ESP_LOGI(TAG, "AMS kész: feliratkozva a lejátszóra és a számra");
            return 0;
        }
        default: return 0;
    }
    next_step();
    return 0;
}

// Mit csinál: a csonkolt érték teljes változatának beolvasása után beírja az adatot.
int read_full_cb(uint16_t, const ble_gatt_error* err, ble_gatt_attr* attr, void*) {
    if (err->status == 0 && attr && attr->om) {
        uint8_t buf[200];
        const uint16_t len = OS_MBUF_PKTLEN(attr->om);
        const uint16_t n = len > sizeof buf ? sizeof buf : len;
        os_mbuf_copydata(attr->om, 0, n, buf);
        apply(g_trunc_ent, g_trunc_attr, buf, n);
    }
    g_trunc_busy = false;
    return 0;
}

int trunc_write_cb(uint16_t conn, const ble_gatt_error* err, ble_gatt_attr*, void*) {
    if (err->status != 0 || ble_gattc_read(conn, g_h.ea_val, read_full_cb, nullptr) != 0) g_trunc_busy = false;
    return 0;
}

// Mit csinál: a lépéssor soron következő GATT-műveletét indítja.
void next_step() {
    static const uint8_t kNotify[2] = {0x01, 0x00};
    static const uint8_t kPlayer[] = {kEntityPlayer, 0, 1, 2};   // név, lejátszási adat, hangerő
    static const uint8_t kTrack[] = {kEntityTrack, 0, 1, 2, 3};  // előadó, album, cím, hossz
    const uint16_t c = g_conn;
    int rc = 0;
    switch (g_step) {
        case Step::DiscSvc:
            memset(&g_h, 0, sizeof g_h);
            rc = ble_gattc_disc_svc_by_uuid(c, &kAmsSvc.u, svc_cb, nullptr);
            break;
        case Step::DiscChrs:
            rc = ble_gattc_disc_all_chrs(c, g_h.svc_start, g_h.svc_end, chr_cb, nullptr);
            break;
        case Step::DiscDscRc:
            rc = ble_gattc_disc_all_dscs(c, g_h.rc_val + 1, dsc_range_end(g_h.rc_def), dsc_cb, &g_h.rc_cccd);
            break;
        case Step::DiscDscEu:
            rc = ble_gattc_disc_all_dscs(c, g_h.eu_val + 1, dsc_range_end(g_h.eu_def), dsc_cb, &g_h.eu_cccd);
            break;
        case Step::SubRc: rc = ble_gattc_write_flat(c, g_h.rc_cccd, kNotify, sizeof kNotify, write_cb, nullptr); break;
        case Step::SubEu: rc = ble_gattc_write_flat(c, g_h.eu_cccd, kNotify, sizeof kNotify, write_cb, nullptr); break;
        case Step::EntPlayer: rc = ble_gattc_write_flat(c, g_h.eu_val, kPlayer, sizeof kPlayer, write_cb, nullptr); break;
        case Step::EntTrack: rc = ble_gattc_write_flat(c, g_h.eu_val, kTrack, sizeof kTrack, write_cb, nullptr); break;
        default: return;
    }
    if (rc != 0) fail("a GATT-művelet indítása", rc);
}

// Mit csinál: egy Entity Update értesítést dolgoz fel: [entitás, attribútum, jelzők, érték...].
void on_entity_update(const uint8_t* d, size_t n) {
    if (n < 3) return;
    const uint8_t ent = d[0], attr = d[1], flags = d[2];
    apply(ent, attr, d + 3, n - 3);
    // A jelző 0. bitje: az érték csonkolt (kicsi az MTU). A teljes értéket az Entity Attribute-on kérjük el.
    if ((flags & 1) && !g_trunc_busy && g_h.ea_val) {
        g_trunc_busy = true;
        g_trunc_ent = ent;
        g_trunc_attr = attr;
        const uint8_t req[2] = {ent, attr};
        if (ble_gattc_write_flat(g_conn, g_h.ea_val, req, sizeof req, trunc_write_cb, nullptr) != 0) g_trunc_busy = false;
    }
}

// Mit csinál: a kapcsolat eseményeit dolgozza fel: kapcsolódás, titkosítás, értesítések, bontás.
int ams_event(ble_gap_event* ev, void*) {
    switch (ev->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (ev->connect.status == 0) {
                g_conn = ev->connect.conn_handle;
                {
                    Lock l;
                    g_info.connected = true;
                }
                ESP_LOGI(TAG, "Az iPhone csatlakozott; biztonsági kérés küldése");
                ble_gap_security_initiate(g_conn);
            } else {
                advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGW(TAG, "Bontva, ok: %d", ev->disconnect.reason);
            g_conn = BLE_HS_CONN_HANDLE_NONE;
            g_step = Step::Idle;
            g_trunc_busy = false;
            {
                Lock l;
                const uint32_t u = g_info.updates;
                g_info = AmsInfo();
                g_info.updates = u + 1;
            }
            advertise();
            break;
        }
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (ev->enc_change.status == 0) {
                {
                    Lock l;
                    g_info.encrypted = true;
                }
                ESP_LOGI(TAG, "Titkosított kapcsolat; az AMS felderítése");
                g_step = Step::DiscSvc;
                next_step();
            } else {
                ESP_LOGE(TAG, "A titkosítás sikertelen (%d)", ev->enc_change.status);
            }
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // Az iPhone elfelejtette az eszközt, és újra párosítana: a régi kötést töröljük.
            ble_gap_conn_desc d;
            if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_NOTIFY_RX: {
            if (!ev->notify_rx.om) break;
            uint8_t buf[220];
            const uint16_t len = OS_MBUF_PKTLEN(ev->notify_rx.om);
            const uint16_t n = len > sizeof buf ? sizeof buf : len;
            os_mbuf_copydata(ev->notify_rx.om, 0, n, buf);
            const uint16_t h = ev->notify_rx.attr_handle;
            if (h == g_h.eu_val) on_entity_update(buf, n);
            else if (h == g_h.rc_val) ESP_LOGI(TAG, "Támogatott parancsok: %u db", static_cast<unsigned>(n));
            break;
        }
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU: %d", ev->mtu.value);
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            break;
        default:
            break;
    }
    return 0;
}

// Mit csinál: csatlakoztatható hirdetést indít; az adatcsomagban az AMS-t kérő UUID, a válaszcsomagban a név.
// Miért:      a név és a 128 bites UUID együtt nem fér az 31 bájtos hirdetésbe.
void advertise() {
    ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.sol_uuids128 = &kAmsSvc;
    f.sol_num_uuids128 = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields: %d", rc);
        return;
    }
    ble_hs_adv_fields r;
    memset(&r, 0, sizeof r);
    r.name = reinterpret_cast<const uint8_t*>(g_name);
    r.name_len = static_cast<uint8_t>(strlen(g_name));
    r.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&r);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields: %d", rc);
        return;
    }
    ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = p.itvl_max = 160;  // 100 ms
    rc = ble_gap_adv_start(ble_internal_addr_type(), nullptr, BLE_HS_FOREVER, &p, ams_event, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGE(TAG, "adv_start: %d", rc);
}

}  // namespace

bool ble_ams_start(const char* name) {
    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
    g_name = name;
    g_conn = BLE_HS_CONN_HANDLE_NONE;
    g_step = Step::Idle;
    {
        Lock l;
        g_info = AmsInfo();
    }
    if (!ble_internal_start(name, false, true)) return false;
    advertise();
    ESP_LOGI(TAG, "AMS-hirdetés indul: \"%s\"", name);
    return true;
}

AmsInfo ble_ams_info() {
    if (!g_mutex) return AmsInfo();
    Lock l;
    return g_info;
}

bool ble_ams_command(AmsCommand cmd) {
    if (g_step != Step::Ready || g_conn == BLE_HS_CONN_HANDLE_NONE) return false;
    const uint8_t c = static_cast<uint8_t>(cmd);
    return ble_gattc_write_flat(g_conn, g_h.rc_val, &c, 1, nullptr, nullptr) == 0;
}

void ble_ams_forget_bonds() { ble_store_clear(); }

}  // namespace og
