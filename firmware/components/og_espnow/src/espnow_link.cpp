#include "espnow_link.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace og {

namespace {

const char* TAG = "espnow";

constexpr UBaseType_t kQueueDepth = 16;

QueueHandle_t g_rx_queue = nullptr;
std::atomic<uint32_t> g_tx_ok{0}, g_tx_fail{0}, g_rx{0}, g_rx_dropped{0};

// Mit csinál: a rádió jelzése egy küldés végéről; csak számlál.
void on_sent(const esp_now_send_info_t*, esp_now_send_status_t status) {
    (status == ESP_NOW_SEND_SUCCESS ? g_tx_ok : g_tx_fail).fetch_add(1, std::memory_order_relaxed);
}

// Mit csinál: egy vett csomagot bemásol a sorba (a Wi-Fi-feladatban fut, ezért rövid).
void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len <= 0 || len > static_cast<int>(kEspNowMaxPayload)) return;
    EspNowLink::Packet p;
    std::memcpy(p.src, info->src_addr, 6);
    p.rssi = static_cast<int8_t>(info->rx_ctrl->rssi);
    p.len = static_cast<uint8_t>(len);
    p.rx_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    std::memcpy(p.data, data, static_cast<size_t>(len));
    if (xQueueSend(g_rx_queue, &p, 0) == pdTRUE)
        g_rx.fetch_add(1, std::memory_order_relaxed);
    else
        g_rx_dropped.fetch_add(1, std::memory_order_relaxed);
}

// Mit csinál: "AA:BB:CC:DD:EE:FF" alakú szövegből MAC-cím. Hamis, ha a formátum rossz.
bool parse_mac(const char* s, uint8_t out[6]) {
    unsigned v[6];
    if (std::sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; ++i) {
        if (v[i] > 0xFF) return false;
        out[i] = static_cast<uint8_t>(v[i]);
    }
    return true;
}

#define OG_CHECK(expr)                                                  \
    do {                                                                \
        esp_err_t err_ = (expr);                                        \
        if (err_ != ESP_OK) {                                           \
            ESP_LOGE(TAG, "%s: %s", #expr, esp_err_to_name(err_));      \
            return false;                                               \
        }                                                               \
    } while (0)

}  // namespace

bool EspNowLink::init(uint8_t channel, const uint8_t* peer) {
    if (peer) {
        std::memcpy(peer_, peer, 6);
    } else if (!parse_mac(CONFIG_OG_ESPNOW_PEER_MAC, peer_)) {
        ESP_LOGE(TAG, "Hibás társcím a menuconfigban: %s", CONFIG_OG_ESPNOW_PEER_MAC);
        return false;
    }

    // A Wi-Fi-driver az NVS-ben tárolja a kalibrációs adatait.
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        OG_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    OG_CHECK(e);
    OG_CHECK(esp_netif_init());
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) OG_CHECK(e);  // már létezhet

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    OG_CHECK(esp_wifi_init(&wcfg));
    OG_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    OG_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    OG_CHECK(esp_wifi_start());
    OG_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    OG_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    g_rx_queue = xQueueCreate(kQueueDepth, sizeof(Packet));
    if (!g_rx_queue) {
        ESP_LOGE(TAG, "Nincs memória a vételi sorhoz");
        return false;
    }
    OG_CHECK(esp_now_init());
    OG_CHECK(esp_now_register_send_cb(on_sent));
    OG_CHECK(esp_now_register_recv_cb(on_recv));

    esp_now_peer_info_t pi = {};
    std::memcpy(pi.peer_addr, peer_, 6);
    pi.channel = channel;
    pi.ifidx = WIFI_IF_STA;
    pi.encrypt = false;
    OG_CHECK(esp_now_add_peer(&pi));

    uint8_t mac[6];
    own_mac(mac);
    ESP_LOGI(TAG, "ESP-NOW kész: csatorna %u, saját MAC " MACSTR ", társ " MACSTR "%s", channel, MAC2STR(mac),
             MAC2STR(peer_), (peer_[0] & 0x01) ? " (szórás: nincs nyugta)" : "");
    ready_ = true;
    return true;
}

bool EspNowLink::send(const void* data, size_t len) {
    if (!ready_) return false;
    const esp_err_t e = esp_now_send(peer_, static_cast<const uint8_t*>(data), len);
    if (e != ESP_OK) {
        g_tx_fail.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool EspNowLink::recv(Packet& p, uint32_t timeout_ms) {
    if (!ready_) return false;
    return xQueueReceive(g_rx_queue, &p, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

EspNowLink::Stats EspNowLink::stats() const {
    return Stats{g_tx_ok.load(), g_tx_fail.load(), g_rx.load(), g_rx_dropped.load()};
}

void EspNowLink::own_mac(uint8_t out[6]) const { esp_read_mac(out, ESP_MAC_WIFI_STA); }

}  // namespace og
