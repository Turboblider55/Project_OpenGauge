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
#include <cmath>

#include "board_pins.h"
#include "ecu_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "og_codec.h"
#include "sdkconfig.h"
#include "twai_bus.h"

namespace {

const char* TAG = "og_sim";

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

    DelayedEmitter out(bus);
    uint32_t next_state = bus.now_ms();
    uint32_t next_stats = bus.now_ms() + 10000;
    for (;;) {
        const uint32_t now = bus.now_ms();
        if (static_cast<int32_t>(now - next_state) >= 0) {
            update_state(ecu.state, now);
            next_state = now + 100;
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
        const uint32_t wait = out.ms_until_next(to_state > 0 ? static_cast<uint32_t>(to_state) : 0u);
        og::CanFrame f;
        if (bus.recv(f, wait)) ecu.on_frame(f, out);
    }
}
