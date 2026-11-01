#include "status_led.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace og {

namespace {

const char* TAG = "led";

constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_1;
constexpr ledc_channel_t kChannels[3] = {LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3};
constexpr uint32_t kDutyMax = 1023;  // 10 bit
constexpr uint32_t kFrameMs = 20;
constexpr uint32_t kResetFaultMs = 60000;  // a rendellenes újraindulás jelzése ennyi ideig látszik

inline uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Mit csinál: igaz, ha az előző futás nem szabályosan ért véget.
// Miért:      összeomláskor már semmi sem tud villogni, ezt csak a következő induláskor lehet jelezni.
bool abnormal_reset(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool StatusLed::start(const LedPins& pins) {
    pins_ = pins;
    const esp_reset_reason_t reason = esp_reset_reason();
    if (abnormal_reset(reason)) {
        ESP_LOGW(TAG, "Rendellenes újraindulás (ok: %d)", static_cast<int>(reason));
        set_fault(Fault::Reset, true);
        reset_fault_until_ms_ = now_ms() + kResetFaultMs;
    }
    if (pins_.red < 0 && pins_.green < 0 && pins_.blue < 0) return true;  // ezen a lapon nincs LED

    ledc_timer_config_t t = {};
    t.speed_mode = kMode;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = kTimer;
    t.freq_hz = 1000;
    t.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s", esp_err_to_name(e));
        return false;
    }
    const int gpio[3] = {pins_.red, pins_.green, pins_.blue};
    for (int i = 0; i < 3; ++i) {
        if (gpio[i] < 0) continue;
        ledc_channel_config_t c = {};
        c.gpio_num = gpio[i];
        c.speed_mode = kMode;
        c.channel = kChannels[i];
        c.timer_sel = kTimer;
        c.duty = 0;
        e = ledc_channel_config(&c);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config: %s", esp_err_to_name(e));
            return false;
        }
    }
    return xTaskCreate(task_entry, "og_led", 3072, this, 3, nullptr) == pdPASS;
}

void StatusLed::task_entry(void* self) { static_cast<StatusLed*>(self)->task(); }

// Mit csinál: egy LED kimenetének beállítása a hardverváltozat fényerejével.
void StatusLed::write(int index, bool on) {
    const uint8_t pct[3] = {pins_.red_pct, pins_.green_pct, pins_.blue_pct};
    const uint32_t duty = on ? kDutyMax * pct[index] / 100 : 0;
    ledc_set_duty(kMode, kChannels[index], duty);
    ledc_update_duty(kMode, kChannels[index]);
}

// Mit csinál: a mintát futtatja: kimenetet csak változáskor ír, az állapotváltozást naplózza.
void StatusLed::task() {
    const int gpio[3] = {pins_.red, pins_.green, pins_.blue};
    const uint32_t start = now_ms();
    LedOutput last = {false, false, false};
    LedState logged;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = now_ms();
        if (reset_fault_until_ms_ && static_cast<int32_t>(now - reset_fault_until_ms_) >= 0) {
            set_fault(Fault::Reset, false);
            reset_fault_until_ms_ = 0;
        }

        LedState s;
        s.searching = searching_.load(std::memory_order_relaxed);
        s.link = static_cast<LinkLed>(link_.load(std::memory_order_relaxed));
        s.faults = faults_.load(std::memory_order_relaxed);

        LedOutput o = led_frame(s, now - start);
        if (!enabled_.load(std::memory_order_relaxed)) o = {false, false, false};
        const bool values[3] = {o.red, o.green, o.blue};
        const bool old[3] = {last.red, last.green, last.blue};
        for (int i = 0; i < 3; ++i)
            if (gpio[i] >= 0 && values[i] != old[i]) write(i, values[i]);
        last = o;

        if (s.searching != logged.searching || s.link != logged.link || s.faults != logged.faults) {
            ESP_LOGI(TAG, "Állapot: piros kód %u (hibamaszk 0x%02X), kék %s, zöld %s",
                     static_cast<unsigned>(top_fault(s.faults)), s.faults,
                     s.link == LinkLed::Ok ? "folyamatos" : s.link == LinkLed::Waiting ? "villog" : "ki",
                     s.searching ? "keresés" : "normál");
            logged = s;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kFrameMs));
    }
}

}  // namespace og
