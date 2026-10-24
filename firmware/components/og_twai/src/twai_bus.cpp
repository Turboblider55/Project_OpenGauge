#include "twai_bus.h"

#include <cstdio>
#include <cstring>

#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace og {

namespace {

// Mit csinál: ms -> FreeRTOS tick, felfelé kerekítve.
// Miért:      100 Hz-es tick mellett a pdMS_TO_TICKS(5) nulla lenne, és a várakozás elmaradna.
inline TickType_t ticks(uint32_t ms) {
    return static_cast<TickType_t>((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
}

constexpr uint32_t kAlerts = TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_BUS_OFF |
                             TWAI_ALERT_ERR_PASS | TWAI_ALERT_RX_QUEUE_FULL;

}  // namespace

TwaiBus::TwaiBus(int tx_gpio, int rx_gpio, const char* log_tag)
    : tx_gpio_(tx_gpio), rx_gpio_(rx_gpio), tag_(log_tag) {
#ifdef CONFIG_OG_TWAI_LOG_FRAMES
    log_frames_ = true;
#else
    log_frames_ = false;
#endif
}

TwaiBus::~TwaiBus() { uninstall(); }

// Mit csinál: a drivert a cfg_ szerinti sebességgel és üzemmóddal telepíti és elindítja.
// Hogyan:     a szabványos ESP-IDF időzítés-makrók; minden keretet elfogad (nincs hardveres
//             szűrés), a fogadási sor 32 keretes.
// Miért:      a szűrés nélküli fogadás az első hardveres tesztnél mindent megmutat a naplóban;
//             a hardveres azonosítószűrés (F-OBD-01) a járműbuszon lesz fontos.
bool TwaiBus::install() {
    const twai_mode_t mode = cfg_.listen_only ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL;
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(tx_gpio_),
                                                          static_cast<gpio_num_t>(rx_gpio_), mode);
    g.tx_queue_len = 4;
    g.rx_queue_len = 32;
    g.alerts_enabled = kAlerts;

    const twai_timing_config_t t1000 = TWAI_TIMING_CONFIG_1MBITS();
    const twai_timing_config_t t500 = TWAI_TIMING_CONFIG_500KBITS();
    const twai_timing_config_t t250 = TWAI_TIMING_CONFIG_250KBITS();
    const twai_timing_config_t t125 = TWAI_TIMING_CONFIG_125KBITS();
    const twai_timing_config_t* t = nullptr;
    switch (cfg_.bitrate_bps) {
        case 1000000: t = &t1000; break;
        case 500000: t = &t500; break;
        case 250000: t = &t250; break;
        case 125000: t = &t125; break;
        default:
            ESP_LOGE(tag_, "Nem támogatott CAN-sebesség: %lu bit/s", (unsigned long)cfg_.bitrate_bps);
            return false;
    }
    const twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t e = twai_driver_install(&g, t, &f);
    if (e != ESP_OK) {
        ESP_LOGE(tag_, "twai_driver_install: %s", esp_err_to_name(e));
        return false;
    }
    e = twai_start();
    if (e != ESP_OK) {
        ESP_LOGE(tag_, "twai_start: %s", esp_err_to_name(e));
        twai_driver_uninstall();
        return false;
    }
    installed_ = true;
    return true;
}

void TwaiBus::uninstall() {
    if (!installed_) return;
    twai_stop();  // bus-off állapotban hibát ad, de az eltávolítás úgy is megengedett
    twai_driver_uninstall();
    installed_ = false;
}

// Mit csinál: megszakítja a függő adást, és újra működőképes állapotba hozza a drivert.
// Hogyan:     futó állapotban stop + start (a stop kiüríti a TX-sort, és megszakítja az
//             éppen ismételt keretet); bus-off vagy más állapotban teljes újratelepítés.
// Miért:      nyugtázás nélkül (nincs másik csomópont, vagy rossz a sebesség) a vezérlő a
//             keretet a végtelenségig ismételné, és ezzel elárasztaná a buszt.
void TwaiBus::restart() {
    ++stats_.restarts;
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK && st.state == TWAI_STATE_RUNNING) {
        twai_stop();
        if (twai_start() == ESP_OK) return;
    }
    uninstall();
    install();
}

bool TwaiBus::configure(const CanConfig& cfg) {
    uninstall();
    cfg_ = cfg;
    const bool ok = install();
    if (ok)
        ESP_LOGI(tag_, "TWAI: %lu kbit/s%s", (unsigned long)(cfg.bitrate_bps / 1000),
                 cfg.listen_only ? ", csak hallgató" : "");
    return ok;
}

// Mit csinál: egy keretet ad, és megvárja, hogy egy másik csomópont nyugtázza.
// Hogyan:     a régi riasztásokat eldobja, sorba teszi a keretet, majd a határidőig a
//             TX_SUCCESS / TX_FAILED / BUS_OFF riasztásra vár. Sikertelenségnél újraindít.
// Miért:      a twai_transmit() már a sorba tételkor visszatér; az ICanBus szerződése
//             viszont az, hogy az igaz érték "a keret elment" jelentésű.
bool TwaiBus::send(const CanFrame& f, uint32_t timeout_ms) {
    if (!installed_ || cfg_.listen_only) {
        vTaskDelay(ticks(timeout_ms));  // a VirtualBus is "eltölti" a határidőt
        ++stats_.tx_fail;
        log_frame("TX", f, false);
        return false;
    }
    uint32_t alerts = 0;
    while (twai_read_alerts(&alerts, 0) == ESP_OK) {
    }

    twai_message_t m = {};
    m.extd = f.ext ? 1 : 0;
    m.identifier = f.id;
    m.data_length_code = f.dlc > 8 ? 8 : f.dlc;
    std::memcpy(m.data, f.data, m.data_length_code);

    const uint32_t start = now_ms();
    bool ok = false;
    if (twai_transmit(&m, ticks(timeout_ms)) == ESP_OK) {
        for (;;) {
            const uint32_t el = now_ms() - start;
            if (el >= timeout_ms) break;
            if (twai_read_alerts(&alerts, ticks(timeout_ms - el)) != ESP_OK) break;
            if (alerts & TWAI_ALERT_TX_SUCCESS) {
                ok = true;
                break;
            }
            if (alerts & (TWAI_ALERT_TX_FAILED | TWAI_ALERT_BUS_OFF)) break;
        }
    }
    if (ok) {
        ++stats_.tx_ok;
    } else {
        ++stats_.tx_fail;
        restart();
    }
    log_frame("TX", f, ok);
    return ok;
}

// Mit csinál: legfeljebb timeout_ms-ig vár egy adatkeretre.
// Hogyan:     a driver fogadási sorából olvas; a távkérő (RTR) kereteket átugorja, mert az
//             OBD-II nem használja őket.
bool TwaiBus::recv(CanFrame& f, uint32_t timeout_ms) {
    if (!installed_) {
        vTaskDelay(ticks(timeout_ms));
        return false;
    }
    const uint32_t start = now_ms();
    for (;;) {
        const uint32_t el = now_ms() - start;
        const uint32_t rem = el >= timeout_ms ? 0 : timeout_ms - el;
        twai_message_t m;
        if (twai_receive(&m, ticks(rem)) != ESP_OK) return false;
        if (m.rtr) {
            if (rem == 0) return false;
            continue;
        }
        f.id = m.identifier;
        f.ext = m.extd != 0;
        f.dlc = m.data_length_code > 8 ? 8 : m.data_length_code;
        std::memset(f.data, 0, sizeof f.data);
        std::memcpy(f.data, m.data, f.dlc);
        ++stats_.rx;
        log_frame("RX", f, true);
        return true;
    }
}

uint32_t TwaiBus::now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Mit csinál: egy sort ír a keretről: irány, azonosító, DLC, adatbájtok hexában.
// Hogyan:     11 bites azonosító 3, 29 bites 8 hexa jeggyel; a nyugtázatlan adás figyelmeztetés.
// Miért:      a két csomópont naplója így soronként összevethető (logikai analizátor nélkül is).
void TwaiBus::log_frame(const char* dir, const CanFrame& f, bool ok) const {
    if (!log_frames_) return;
    char hex[3 * 8 + 1];
    size_t p = 0;
    for (uint8_t i = 0; i < f.dlc && i < 8; ++i)
        p += std::snprintf(hex + p, sizeof hex - p, i ? " %02X" : "%02X", f.data[i]);
    hex[p] = '\0';
    if (ok) {
        if (f.ext)
            ESP_LOGI(tag_, "%s %08lX [%u] %s", dir, (unsigned long)f.id, f.dlc, hex);
        else
            ESP_LOGI(tag_, "%s %03lX      [%u] %s", dir, (unsigned long)f.id, f.dlc, hex);
    } else {
        ESP_LOGW(tag_, "%s %0*lX [%u] %s  -- nincs nyugtázás", dir, f.ext ? 8 : 3, (unsigned long)f.id,
                 f.dlc, hex);
    }
}

}  // namespace og
