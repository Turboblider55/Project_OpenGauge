// Kijelzőegység – ESP-NOW vételi teszt (M4), kijelző nélkül.
//
// Mit csinál: fogadja az OBD-egység TelemetryMsg csomagjait, és másodpercenként naplózza a
//             vételi statisztikát (csomag/s, összes, elveszett, veszteségarány, elavult, adó-
//             újraindulás, térerő) és az utolsó érvényes értékeket.
// Hogyan:     az EspNowLink sorából olvas; a fejléc és a típus ellenőrzése után a sorszámot a
//             LinkStats dolgozza fel (hardverfüggetlen, PC-n tesztelt). Az érvénytelen
//             (nem a mi formátumunk szerinti) csomagokat külön számolja.
// Miért:      a rádiókapcsolat minősége (veszteség, térerő) a kijelző nélkül is mérhető, így a
//             felület bekötése előtt kiderül, megbízható-e a csatorna.
#include "espnow_rx_test.h"

#include <cstdio>

#include "esp_log.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "og_codec.h"
#include "og_link_stats.h"
#include "sdkconfig.h"

namespace og {

namespace {

const char* TAG = "og_rx";

// Mit csinál: az utolsó telemetriát egy sorban írja ki; az érvénytelen mezők helyén "--".
void log_telemetry(const TelemetryMsg& m) {
    char line[160];
    int p = 0;
    auto put = [&](uint16_t flag, const char* fmt, double v) {
        if (p >= static_cast<int>(sizeof line)) return;
        p += (m.flags & flag) ? std::snprintf(line + p, sizeof line - p, fmt, v)
                              : std::snprintf(line + p, sizeof line - p, "%s", "-- | ");
    };
    put(kFlagRpm, "%.0f 1/perc | ", m.rpm_q4 / 4.0);
    put(kFlagSpeed, "%.0f km/h | ", static_cast<double>(m.speed_kmh));
    put(kFlagCoolant, "víz %.0f °C | ", static_cast<double>(m.coolant_c));
    put(kFlagThrottle, "fojtó %.0f %% | ", m.throttle_raw * 100.0 / 255.0);
    put(kFlagVbat, "%.2f V | ", m.vbat_mv / 1000.0);
    if (p < static_cast<int>(sizeof line)) {
        if (m.flags & kFlagMil)
            std::snprintf(line + p, sizeof line - p, "MIL %s, DTC %u", (m.mil_dtc & 0x80) ? "BE" : "ki", m.mil_dtc & 0x7F);
        else
            std::snprintf(line + p, sizeof line - p, "MIL --");
    }
    ESP_LOGI(TAG, "  %s", line);
}

}  // namespace

void run_espnow_rx_test() {
    static EspNowLink link;
    if (!link.init(CONFIG_OG_ESPNOW_CHANNEL)) {
        ESP_LOGE(TAG, "Az ESP-NOW nem indult el");
        return;
    }

    LinkStats st;
    TelemetryMsg last = {};
    bool have_last = false;
    uint32_t invalid = 0, per_sec = 0, last_rx_ms = 0;
    int rssi_sum = 0;
    TickType_t next = xTaskGetTickCount() + pdMS_TO_TICKS(1000);

    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        const uint32_t wait = now < next ? pdTICKS_TO_MS(next - now) : 0;
        EspNowLink::Packet p;
        if (link.recv(p, wait)) {
            TelemetryMsg m;
            if (decode(p.data, p.len, m)) {
                if (st.on_seq(m.h.seq)) {
                    last = m;
                    have_last = true;
                }
                ++per_sec;
                rssi_sum += p.rssi;
                last_rx_ms = p.rx_ms;
            } else {
                ++invalid;
            }
            continue;
        }

        // Másodpercenkénti összegzés.
        next += pdMS_TO_TICKS(1000);
        const EspNowLink::Stats ls = link.stats();
        if (per_sec == 0) {
            ESP_LOGW(TAG, "Nincs csomag az utolsó 1 s-ban (összes vett %lu, utolsó %lu ms-nál)",
                     (unsigned long)st.received(), (unsigned long)last_rx_ms);
        } else {
            ESP_LOGI(TAG,
                     "%lu csomag/s, RSSI %d dBm | összes %lu, elveszett %lu (%.2f %%), elavult %lu, "
                     "adó-újraindulás %lu, érvénytelen %lu, sor tele %lu, utolsó seq %u",
                     (unsigned long)per_sec, rssi_sum / static_cast<int>(per_sec), (unsigned long)st.received(),
                     (unsigned long)st.lost(), st.loss_ratio() * 100.0, (unsigned long)st.stale(),
                     (unsigned long)st.resyncs(), (unsigned long)invalid, (unsigned long)ls.rx_dropped,
                     st.last_seq());
            if (have_last) log_telemetry(last);
        }
        per_sec = 0;
        rssi_sum = 0;
    }
}

}  // namespace og
