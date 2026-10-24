// OBD-egység (ESP32-C3): protokoll-felismerés és ciklikus PID-lekérdezés valódi CAN-buszon.
//
// Mit csinál: megkeresi a busz beállításait (sebesség, azonosítóhossz) és a protokollt,
//             kiolvassa a VIN-t és a hibakódok számát, majd a PID-terv szerint ciklikusan
//             lekérdezi a fordulatszámot, sebességet stb., és másodpercenként összesítést ír.
//             Minden CAN-keretet naplóz (TwaiBus).
// Hogyan:     ugyanazok az osztályok (obd_detect, IsoTp, ObdClient, PidScheduler), mint a
//             PC-s teszteken, csak a VirtualBus helyett TwaiBus-szal. Ha a fordulatszám
//             egymás után sokszor nem jön, a felismerés újraindul (pl. kihúzott kábel).
// Miért:      ez az első hardveres teszt (S2 ECU-szimulátor <-> C3): azt igazolja, hogy a PC-n
//             tesztelt logika a valódi buszon is működik. Az ESP-NOW-adás később kerül ide.
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "isotp.h"
#include "obd_client.h"
#include "obd_detect.h"
#include "og_codec.h"
#include "pid_scheduler.h"
#include "pid_table.h"
#include "twai_bus.h"

namespace {

const char* TAG = "og_obd";

// A PID-terv (fejlesztési napló, Szoftver lap): PID és frissítési periódus (ms).
// A label az összesítő naplósor rövid felirata (mértékegységgel).
struct PlanItem {
    uint8_t pid;
    uint32_t period_ms;
    const char* label;
};
constexpr PlanItem kPlan[] = {
    {0x0C, 100, "1/perc"},    // fordulatszám 10 Hz
    {0x0D, 200, "km/h"},      // sebesség 5 Hz
    {0x11, 333, "fojtó %"},   // fojtószelep 3 Hz
    {0x05, 1000, "víz °C"},   // hűtővíz 1 Hz
    {0x42, 2000, "U V"},      // modulfeszültség 0,5 Hz
    {0x01, 5000, "MIL"},      // MIL és hibakódok száma 0,2 Hz
};

// Ennyi egymást követő sikertelen fordulatszám-lekérdezés után újraindul a felismerés.
constexpr uint32_t kLostAfterFailures = 10;

// Mit csinál: a CAN-adó-vevőt nagy sebességű (normál) üzemmódba kapcsolja, ha van RS lába.
// Miért:      az SN65HVD231 RS lába HIGH szinten készenlétbe teszi az adó-vevőt; a saját
//             NYÁK-on ezt az IO10 vezérli. A tesztmodulon nincs ilyen láb.
void transceiver_enable() {
    if (og::board::kPinCanRs < 0) return;
    const gpio_num_t rs = static_cast<gpio_num_t>(og::board::kPinCanRs);
    gpio_reset_pin(rs);
    gpio_set_direction(rs, GPIO_MODE_OUTPUT);
    gpio_set_level(rs, 0);
}

// Mit csinál: megmondja, hogy a felismeréskor kiolvasott bitmap szerint támogatott-e a PID.
// Hogyan:     a 01 00 / 01 20 / 01 40 blokkok közül a PID-hez tartozót nézi.
// Miért:      nem támogatott PID-re az ECU nem válaszol, a kérés csak időt pazarolna.
bool pid_supported(const og::ProtocolInfo& info, uint8_t pid) {
    if (pid == 0) return true;
    const int block = (pid - 1) / 0x20;
    if (block > 2 || !info.has_supported[block]) return false;
    return og::pid_in_bitmap(info.supported[block], static_cast<uint8_t>(block * 0x20), pid);
}

// Mit csinál: a felismerés eredményét egy-két sorban naplózza.
void log_protocol(const og::ProtocolInfo& info) {
    ESP_LOGI(TAG, "Protokoll: %s, %lu kbit/s, %s azonosítók, %u ECU, elsődleges kérés-azonosító 0x%lX",
             info.kind == og::ProtocolKind::Classic ? "klasszikus J1979" : "OBDonUDS (J1979-2)",
             (unsigned long)(info.bitrate_bps / 1000), info.ext_ids ? "29 bites" : "11 bites", info.ecu_count,
             (unsigned long)info.primary_request_id);
    for (uint8_t i = 0; i < info.ecu_count; ++i)
        ESP_LOGI(TAG, "  ECU %u: válasz-azonosító 0x%lX", i, (unsigned long)info.ecu_response_id[i]);
    for (int b = 0; b < 3; ++b)
        if (info.has_supported[b])
            ESP_LOGI(TAG, "  Támogatott PID-ek 0x%02X: %08lX", b * 0x20, (unsigned long)info.supported[b]);
}

// Mit csinál: egy sorban kiírja a PID-ek utolsó értékét és az életkorát.
// Hogyan:     500 ms-nál régebbi értéket "(elavult)" jelöléssel ír ki; ami még nem jött meg, "--".
void log_values(const og::PidScheduler& s, uint32_t now) {
    char line[200];
    size_t p = 0;
    for (const PlanItem& it : kPlan) {
        if (it.pid == 0x01) continue;  // külön, MIL-ként írjuk ki
        float v = 0.f;
        uint32_t age = 0;
        if (s.value(it.pid, v, now, age))
            p += snprintf(line + p, sizeof line - p, "%s %.1f%s | ", it.label, v, age > 500 ? " (elavult)" : "");
        else
            p += snprintf(line + p, sizeof line - p, "%s -- | ", it.label);
        if (p >= sizeof line) break;
    }
    og::MilStatus m;
    if (p < sizeof line) {
        if (s.mil(m))
            snprintf(line + p, sizeof line - p, "MIL %s, DTC %u", m.mil_on ? "BE" : "ki", m.dtc_count);
        else
            snprintf(line + p, sizeof line - p, "MIL --");
    }
    ESP_LOGI(TAG, "%s", line);
}

// Mit csinál: a felismert ECU-val lekérdezési ciklust futtat, amíg a kapcsolat él.
// Hogyan:     egyszeri lekérdezések (VIN, hibakódszám), majd PidScheduler; ha nincs esedékes
//             PID, alszik a következőig. Visszatér, ha a fordulatszám tartósan nem jön.
void run_session(og::TwaiBus& bus, const og::ProtocolInfo& info) {
    og::IsoTpConfig tc;
    tc.ext_ids = info.ext_ids;
    og::IsoTp tp(bus, tc);
    og::ObdClient cl(tp);
    cl.set_target(info.primary_request_id);

    char vin[18];
    og::Err e = cl.read_vin(vin);
    if (e == og::Err::Ok)
        ESP_LOGI(TAG, "VIN: %s", vin);
    else
        ESP_LOGW(TAG, "VIN nem olvasható: %s", og::err_name(e));

    uint8_t dtc = 0;
    e = cl.read_dtc_count(dtc);
    if (e == og::Err::Ok)
        ESP_LOGI(TAG, "Tárolt hibakódok (Mode 03): %u", dtc);
    else
        ESP_LOGW(TAG, "Mode 03 sikertelen: %s", og::err_name(e));

    og::PidScheduler s;
    const uint32_t start = cl.now_ms();
    size_t added = 0;
    for (const PlanItem& it : kPlan) {
        if (!pid_supported(info, it.pid)) {
            ESP_LOGW(TAG, "PID 0x%02X nem támogatott, kimarad", it.pid);
            continue;
        }
        if (s.add(it.pid, it.period_ms, start)) ++added;
    }
    if (added == 0) {
        ESP_LOGE(TAG, "Egyetlen tervezett PID sem támogatott");
        vTaskDelay(pdMS_TO_TICKS(5000));
        return;
    }

    uint32_t next_log = start + 1000;
    for (;;) {
        og::Err err;
        if (!s.step(cl, err)) {
            const uint32_t w = s.ms_until_next(cl.now_ms());
            vTaskDelay(pdMS_TO_TICKS(w ? w : 1));
        }
        const uint32_t now = cl.now_ms();
        if (static_cast<int32_t>(now - next_log) >= 0) {
            log_values(s, now);
            const og::TwaiBus::Stats& st = bus.stats();
            ESP_LOGI(TAG, "Kérések: RPM %lu (hiba %lu), összes TX %lu, RX %lu, újraindítás %lu",
                     (unsigned long)s.requests(0x0C), (unsigned long)s.failures(0x0C), (unsigned long)st.tx_ok,
                     (unsigned long)st.rx, (unsigned long)st.restarts);
            next_log = now + 1000;
        }
        if (pid_supported(info, 0x0C) && s.consecutive_failures(0x0C) >= kLostAfterFailures) {
            ESP_LOGW(TAG, "A fordulatszám %lu kérés óta nem jön: a kapcsolat elveszett, új felismerés",
                     (unsigned long)kLostAfterFailures);
            return;
        }
    }
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "OpenGauge OBD-egység indul; protokoll v%d, Telemetry = %u B", og::kProtoVersion,
             (unsigned)sizeof(og::TelemetryMsg));
    transceiver_enable();

    og::TwaiBus bus(og::board::kPinCanTx, og::board::kPinCanRx, "can");
    for (;;) {
        ESP_LOGI(TAG, "Protokoll-felismerés...");
        og::ProtocolInfo info;
        const og::Err e = og::obd_detect(bus, og::DetectConfig(), info);
        if (e == og::Err::NotSupported) {
            log_protocol(info);
            ESP_LOGW(TAG, "OBDonUDS (J1979-2) jármű: az eszköz ezt nem támogatja");
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        if (e != og::Err::Ok) {
            ESP_LOGW(TAG, "Nincs válasz egyik beállításon sem (%s), újra 1 s múlva", og::err_name(e));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        log_protocol(info);
        run_session(bus, info);
    }
}
