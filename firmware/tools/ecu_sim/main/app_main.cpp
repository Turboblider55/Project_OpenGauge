// ECU-szimulátor (ESP32-S2): a PC-s tesztekben használt EcuModel valódi CAN-buszon.
//
// Mit csinál: egy OBD-II motorvezérlőt játszik el: a buszról érkező kérésekre (Mode 01/03/09,
//             protokoll-felismerés) a modell szerinti válaszokat adja, és a járműállapotot
//             (fordulatszám, sebesség, hűtővíz stb.) lassan változtatja, hogy az OBD-egység
//             kijelzőjén mozgó értékek legyenek. Minden keretet naplóz (TwaiBus).
// Hogyan:     egyetlen ciklus: fogad (legfeljebb a következő esedékes válaszig), a keretet
//             átadja a modellnek, az esedékes válaszokat kiküldi, és 100 ms-onként frissíti
//             az állapotot. A modell késleltetett válaszait a DelayedEmitter tárolja.
// Miért:      így a C3-on futó OBD-kód jármű nélkül, asztalon tesztelhető, és pontosan
//             ugyanazzal az ECU-viselkedéssel, mint a PC-s tesztekben.
//
// Konzol (USB-soros, soronként): "key on|off" (gyújtás: kikapcsolva a busz csendes és az ECU
//             nem válaszol, bekapcsolva 20 ms-onként háttérforgalmat ad), "auto on|off" (az
//             értékek automatikus változása), "rpm|speed|coolant|throttle|vbat <szám>" (kézi
//             érték, az automatikus változás kikapcsolásával), "status", "help".
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "board_pins.h"
#include "ecu_model.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "espnow_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "og_codec.h"
#include "og_protocol.h"
#include "sim_command.h"
#include "sdkconfig.h"
#include "twai_bus.h"

namespace {

const char* TAG = "og_sim";

std::atomic<uint32_t> g_key_on_ms{0};   // a gyújtás bekapcsolásának ideje (esp_timer, ms)
std::atomic<bool> g_wait_display{false};  // a gyújtás óta még nem láttunk a kijelzőtől életjelet

inline uint32_t esp_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Mit csinál: megfigyeli a rádiót (szórt ESP-NOW csomagok), és méri az ébredési láncot.
// Hogyan:     a Telemetria feladója az OBD-egység (a MAC-ját megjegyzi); más feladótól jövő életjel
//             a kijelzőé. A gyújtás bekapcsolása után az első kijelző-életjel idejét kiírja;
//             az OBD-egység ALVÁS üzenetét is jelzi.
// Miért:      mindkét egység naplója elnémul light sleep után (H-33), a rádió kívülről látszik.
void radio_observer_task(void*) {
    static og::EspNowLink link;
    if (!link.init(CONFIG_OG_ESPNOW_CHANNEL)) {
        ESP_LOGE(TAG, "Az ESP-NOW megfigyelő nem indult el");
        vTaskDelete(nullptr);
    }
    uint8_t obd_mac[6] = {};
    bool have_obd = false;
    for (;;) {
        og::EspNowLink::Packet p;
        if (!link.recv(p, 1000)) continue;
#ifdef CONFIG_OG_SIM_RADIO_TRACE
        ESP_LOGI(TAG, "RÁDIÓ: típus 0x%02X, hossz %u, feladó ..:%02X, RSSI %d, %lu ms a gyújtás óta", p.data[1], p.len,
                 p.src[5], p.rssi, (unsigned long)(p.rx_ms - g_key_on_ms.load()));
#endif
        og::TelemetryMsg t;
        og::HeartbeatMsg hb;
        og::SleepMsg sl;
        if (og::decode(p.data, p.len, t)) {
            memcpy(obd_mac, p.src, 6);
            have_obd = true;
        } else if (og::decode(p.data, p.len, sl)) {
            ESP_LOGI(TAG, "RÁDIÓ: %s üzenet az OBD-egységtől (%lu ms a gyújtás óta)",
                     sl.reason == static_cast<uint8_t>(og::SleepReason::EngineStopped) ? "MOTOR LEÁLLT" : "ALVÁS",
                     (unsigned long)(p.rx_ms - g_key_on_ms.load()));
        } else if (og::decode(p.data, p.len, hb)) {
            const bool from_obd = have_obd && memcmp(obd_mac, p.src, 6) == 0;
            if (!from_obd && g_wait_display.exchange(false)) {
                ESP_LOGI(TAG, "ÉBREDÉS: az első kijelző-életjel %lu ms-mal a gyújtás bekapcsolása után",
                         (unsigned long)(p.rx_ms - g_key_on_ms.load()));
            }
        }
    }
}

QueueHandle_t g_cmd_queue = nullptr;  // a konzol parancsai a fő ciklusnak (az ECU-modellt csak az érinti)

// A gyújtás kikapcsolt állapotában az ECU nem válaszol és a busz csendes; bekapcsolva a
// háttérforgalom ennyi ms-onként megy (a valódi autók buszán ennél sűrűbb a forgalom).
constexpr uint32_t kBackgroundPeriodMs = 20;
constexpr uint32_t kBackgroundId = 0x2F0;

// A modell kimenő kereteit tárolja, és az esedékességük szerint küldi ki.
//
// Mit csinál: az emit(f, delay) a keretet "most + delay" időpontra ütemezi; a flush() a már
//             esedékeseket a beérkezés sorrendjében a buszra adja.
// Hogyan:     kis, rögzített méretű tömb (egy VIN-válasz is legfeljebb 3 keret).
// Miért:      a PC-n ezt a VirtualBus végzi a virtuális órán; itt a valós idő számít.
class DelayedEmitter : public og::Emitter {
public:
    explicit DelayedEmitter(og::ICanBus& bus) : bus_(bus) {}

    void emit(const og::CanFrame& f, uint32_t delay_ms) override {
        if (n_ == kCap) {
            ESP_LOGW(TAG, "Kimenő sor tele, keret eldobva (0x%lX)", (unsigned long)f.id);
            return;
        }
        q_[n_++] = Item{f, bus_.now_ms() + delay_ms};
    }

    // Mit csinál: kiküldi az összes esedékes keretet (a sorrendjük megmarad).
    void flush() {
        const uint32_t now = bus_.now_ms();
        size_t w = 0;
        for (size_t i = 0; i < n_; ++i) {
            if (static_cast<int32_t>(q_[i].due - now) <= 0)
                bus_.send(q_[i].frame, 10);  // nyugta nélkül (nincs teszter) a TwaiBus jelzi
            else
                q_[w++] = q_[i];
        }
        n_ = w;
    }

    // Hány ms múlva esedékes a következő keret (limit, ha nincs ennél korábbi).
    uint32_t ms_until_next(uint32_t limit) const {
        const uint32_t now = bus_.now_ms();
        uint32_t best = limit;
        for (size_t i = 0; i < n_; ++i) {
            const int32_t d = static_cast<int32_t>(q_[i].due - now);
            const uint32_t w = d > 0 ? static_cast<uint32_t>(d) : 0u;
            if (w < best) best = w;
        }
        return best;
    }

private:
    struct Item {
        og::CanFrame frame;
        uint32_t due;
    };
    static constexpr size_t kCap = 16;
    og::ICanBus& bus_;
    Item q_[kCap];
    size_t n_ = 0;
};

// Mit csinál: a szimulált járműállapotot az idő függvényében állítja be.
// Hogyan:     20 s-os periódusú "gázadás": a fordulatszám 850 és 3350 1/perc között
//             szinuszosan változik, a sebesség és a fojtószelep ezt követi; a hűtővíz 2 perc
//             alatt 40 °C-ról 90 °C-ra melegszik; az akkufeszültség járó motornál 14,1 V.
// Miért:      a kijelzőn és a naplóban így látszik, hogy az értékek frissülnek, és a
//             dekódolás a teljes tartományban helyes.
void update_state(og::VehicleState& s, uint32_t now_ms) {
    const float t = static_cast<float>(now_ms) / 1000.f;
    const float phase = 0.5f - 0.5f * std::cos(2.f * static_cast<float>(M_PI) * t / 20.f);  // 0..1
    s.rpm = 850.f + 2500.f * phase;
    s.speed_kmh = 90.f * phase;
    s.throttle_pct = 12.f + 60.f * phase;
    s.coolant_c = t >= 120.f ? 90.f : 40.f + 50.f * t / 120.f;
    s.vbat_v = 14.1f;
}

// Mit csinál: a konzolról sorokat olvas, értelmezi őket, és a parancsokat a fő ciklusnak adja.
// Hogyan:     a stdin lekérdezéses olvasása (a USB-CDC konzol nem blokkol), 20 ms-os szünetekkel.
void console_task(void*) {
    char line[48];
    size_t n = 0;
    for (;;) {
        const int c = getchar();
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            line[n] = 0;
            n = 0;
            const og::SimCommand cmd = og::parse_sim_command(line);
            if (cmd.cmd != og::SimCmd::None) xQueueSend(g_cmd_queue, &cmd, 0);
        } else if (n < sizeof line - 1) {
            line[n++] = static_cast<char>(c);
        }
    }
}

const char* kHelp =
    "Parancsok: key on|off | auto on|off | rpm|speed|coolant|throttle|vbat <szám> | status | help";

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "OpenGauge ECU-szimulátor (S2) indul; protokoll v%d", og::kProtoVersion);

    og::TwaiBus bus(og::board::kPinCanTx, og::board::kPinCanRx, "can");
    og::CanConfig cc;
    cc.bitrate_bps = CONFIG_OG_SIM_BITRATE_BPS;
    while (!bus.configure(cc)) {
        ESP_LOGE(TAG, "A TWAI nem indult el, újrapróbálás 1 s múlva");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    og::EcuConfig ec;
#ifdef CONFIG_OG_SIM_EXT_IDS
    ec.ext_ids = true;
#endif
#ifdef CONFIG_OG_SIM_UDS_ONLY
    ec.protocol = og::EcuProtocol::UdsOnly;
#endif
    ec.latency_ms = CONFIG_OG_SIM_LATENCY_MS;
    og::EcuModel ecu(ec);
    ecu.state.dtc_count = CONFIG_OG_SIM_DTC_COUNT;
    ecu.state.mil_on = CONFIG_OG_SIM_DTC_COUNT > 0;

    ESP_LOGI(TAG, "ECU: %s, %s azonosítók, kérés 0x%lX, válasz 0x%lX, válaszidő %lu ms, VIN %s",
             ec.protocol == og::EcuProtocol::UdsOnly ? "OBDonUDS" : "klasszikus J1979",
             ec.ext_ids ? "29 bites" : "11 bites", (unsigned long)ecu.request_id(),
             (unsigned long)ecu.response_id(), (unsigned long)ec.latency_ms, ecu.state.vin);

    g_cmd_queue = xQueueCreate(8, sizeof(og::SimCommand));
    xTaskCreate(console_task, "og_console", 4096, nullptr, 2, nullptr);
    xTaskCreate(radio_observer_task, "og_radio_obs", 4096, nullptr, 2, nullptr);
    ESP_LOGI(TAG, "%s", kHelp);

    DelayedEmitter out(bus);
    bool key_on = true;
    bool auto_values = true;
    uint32_t bg_seq = 0;
    // Az ébredési késleltetés mérése: a gyújtás bekapcsolásától az első nyugtázott keretig (a vevő
    // TWAI-ja él) és az első vett OBD-kérésig eltelt idő.
    bool wait_ack = false, wait_req = false;
    uint32_t key_on_ms = 0, ack_base = 0;
    uint32_t next_bg = bus.now_ms();
    uint32_t next_state = bus.now_ms();
    uint32_t next_stats = bus.now_ms() + 10000;
    for (;;) {
        const uint32_t now = bus.now_ms();
        og::SimCommand cmd;
        while (xQueueReceive(g_cmd_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.cmd) {
                case og::SimCmd::KeyOn:
                    key_on = true;
                    next_bg = now;
                    key_on_ms = now;
                    g_key_on_ms.store(esp_ms());
                    g_wait_display.store(true);
                    ack_base = bus.stats().tx_ok;
                    wait_ack = wait_req = true;
                    ESP_LOGI(TAG, "GYÚJTÁS BE (uptime %lu ms)", (unsigned long)now);
                    break;
                case og::SimCmd::KeyOff:
                    key_on = false;
                    ESP_LOGI(TAG, "GYÚJTÁS KI (uptime %lu ms): az ECU nem válaszol, a busz csendes", (unsigned long)now);
                    break;
                case og::SimCmd::AutoOn:
                    auto_values = true;
                    ESP_LOGI(TAG, "Automatikus értékváltozás: be");
                    break;
                case og::SimCmd::AutoOff:
                    auto_values = false;
                    ESP_LOGI(TAG, "Automatikus értékváltozás: ki (az értékek megmaradnak)");
                    break;
                case og::SimCmd::Set: {
                    auto_values = false;
                    og::VehicleState& s = ecu.state;
                    switch (cmd.field) {
                        case og::SimField::Rpm: s.rpm = cmd.value; break;
                        case og::SimField::Speed: s.speed_kmh = cmd.value; break;
                        case og::SimField::Coolant: s.coolant_c = cmd.value; break;
                        case og::SimField::Throttle: s.throttle_pct = cmd.value; break;
                        case og::SimField::Vbat: s.vbat_v = cmd.value; break;
                    }
                    ESP_LOGI(TAG, "Kézi érték beállítva (automatikus változás ki): %.1f", cmd.value);
                    break;
                }
                case og::SimCmd::Status:
                    ESP_LOGI(TAG, "Gyújtás %s, automatikus változás %s | RPM %.0f, sebesség %.0f, víz %.0f, fojtó %.0f, U %.1f V",
                             key_on ? "BE" : "KI", auto_values ? "be" : "ki", ecu.state.rpm, ecu.state.speed_kmh,
                             ecu.state.coolant_c, ecu.state.throttle_pct, ecu.state.vbat_v);
                    break;
                case og::SimCmd::Help:
                    ESP_LOGI(TAG, "%s", kHelp);
                    break;
                case og::SimCmd::Bad:
                    ESP_LOGW(TAG, "Értelmezhetetlen parancs. %s", kHelp);
                    break;
                default:
                    break;
            }
        }
        if (key_on && wait_ack && bus.stats().tx_ok != ack_base) {
            wait_ack = false;
            ESP_LOGI(TAG, "ÉBREDÉS: az első nyugtázott keret %lu ms-mal a gyújtás bekapcsolása után",
                     (unsigned long)(now - key_on_ms));
        }
        if (static_cast<int32_t>(now - next_state) >= 0) {
            if (auto_values) update_state(ecu.state, now);
            next_state = now + 100;
        }
        if (key_on && static_cast<int32_t>(now - next_bg) >= 0) {
            // Háttérforgalom: egy nem OBD-azonosítójú keret (a valódi autók buszán ilyenek mennek).
            og::CanFrame bg;
            bg.id = kBackgroundId;
            bg.dlc = 8;
            for (int i = 0; i < 4; ++i) bg.data[i] = static_cast<uint8_t>(bg_seq >> (8 * i));
            ++bg_seq;
            bus.set_frame_log(false);
            bus.send(bg, 2);
            bus.set_frame_log(true);
            next_bg = now + kBackgroundPeriodMs;
        }
        if (static_cast<int32_t>(now - next_stats) >= 0) {
            const og::TwaiBus::Stats& st = bus.stats();
            ESP_LOGI(TAG, "Statisztika: RX %lu, TX ok %lu, TX hiba %lu, újraindítás %lu; RPM %.0f",
                     (unsigned long)st.rx, (unsigned long)st.tx_ok, (unsigned long)st.tx_fail,
                     (unsigned long)st.restarts, ecu.state.rpm);
            next_stats = now + 10000;
        }

        out.flush();
        const int32_t to_state = static_cast<int32_t>(next_state - bus.now_ms());
        uint32_t wait = out.ms_until_next(to_state > 0 ? static_cast<uint32_t>(to_state) : 0u);
        if (key_on) {
            const int32_t to_bg = static_cast<int32_t>(next_bg - bus.now_ms());
            const uint32_t w_bg = to_bg > 0 ? static_cast<uint32_t>(to_bg) : 0u;
            if (w_bg < wait) wait = w_bg;
        } else if (wait > 20) {
            wait = 20;  // gyújtás nélkül is időben észrevegye a konzolparancsot
        }
        og::CanFrame f;
        if (bus.recv(f, wait) && key_on) {
            if (wait_req) {
                wait_req = false;
                ESP_LOGI(TAG, "ÉBREDÉS: az első OBD-kérés %lu ms-mal a gyújtás bekapcsolása után",
                         (unsigned long)(bus.now_ms() - key_on_ms));
            }
            ecu.on_frame(f, out);
        }
    }
}
