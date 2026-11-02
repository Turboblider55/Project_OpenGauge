// Kijelzőegység – alvás- és ébredési teszt (S-12/S-13), kijelző nélkül.
//
// Mit csinál: ébren másodpercenként életjelet küld az OBD-egységnek és figyeli a csomagjait. Ha az
//             OBD-egység ALVÁS üzenetet küld, a rádiót leállítja és light sleepbe lép; az alvási
//             szakaszok után rövid BLE-szkennelő ablakot nyit. Ha ebben az OBD-egység hirdetését
//             látja, felébred: újraindítja az ESP-NOW-t, és életjelet küld.
// Hogyan:     a BLE-vermet csak az ablak idejére indítja el (és állítja le), így alvás közben a
//             BLE-vezérlő nem fut. Az ablak hossza és az alvási szakasz hossza a menuconfigból jön.
// Miért:      a kijelző nem tud busz-forgalomra ébredni (nincs CAN-kapcsolata); az ébresztést az
//             OBD-egység BLE-hirdetése adja (T-14). Az első változat a késleltetés mérésére való,
//             az áramfelvétel finomítása (a BLE-vezérlő saját alvása, H-15) később következik.
#include "wake_test.h"

#include "ble_radio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "og_codec.h"
#include "sdkconfig.h"

namespace og {

namespace {

const char* TAG = "og_wake";

inline uint32_t ms_now() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Mit csinál: alszik, amíg az OBD-egység BLE-hirdetését nem látja; utána újraindítja az ESP-NOW-t.
void sleep_until_ble_wake(EspNowLink& link, StatusLed& led) {
    const uint32_t t0 = ms_now();
    ESP_LOGI(TAG, "ALVÁS üzenet érkezett: light sleep, szkennelő ablak %d ms / %d ms", CONFIG_OG_WAKE_SCAN_WINDOW_MS,
             CONFIG_OG_WAKE_SCAN_PERIOD_MS);
    led.set_enabled(false);
    link.suspend();
    uint32_t cycles = 0;
    for (;;) {
        esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(CONFIG_OG_WAKE_SCAN_PERIOD_MS) * 1000);
        esp_light_sleep_start();
        ++cycles;
        const uint32_t before = ble_scan_stats().adverts;
        if (!ble_start_scanning(100)) {
            ble_stop();
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_OG_WAKE_SCAN_WINDOW_MS));
        const bool seen = ble_scan_stats().adverts != before;
        ble_stop();
        if (seen) break;
    }
    link.resume();
    led.set_enabled(true);
    ESP_LOGI(TAG, "Ébredés: az OBD-egység hirdetése a(z) %lu. ablakban, alvás %lu ms után", (unsigned long)cycles,
             (unsigned long)(ms_now() - t0));
}

}  // namespace

void run_wake_test(StatusLed& led) {
    static EspNowLink link;
    if (!link.init(CONFIG_OG_ESPNOW_CHANNEL)) {
        ESP_LOGE(TAG, "Az ESP-NOW nem indult el");
        return;
    }
    uint16_t seq = 0;
    uint32_t next_hb = ms_now();
    for (;;) {
        const uint32_t now = ms_now();
        if (static_cast<int32_t>(now - next_hb) >= 0) {
            HeartbeatMsg hb = {};
            hb.h = make_header(MsgType::Heartbeat, seq++);
            hb.uptime_s = now / 1000;
            link.send(&hb, sizeof hb);
            next_hb = now + kHeartbeatPeriodMs;
        }
        EspNowLink::Packet p;
        if (link.recv(p, 50)) {
            SleepMsg sm;
            if (decode(p.data, p.len, sm) && sm.reason == static_cast<uint8_t>(SleepReason::Sleep)) {
                sleep_until_ble_wake(link, led);
                next_hb = ms_now();  // ébredés után azonnal életjel
            }
        }
    }
}

}  // namespace og
