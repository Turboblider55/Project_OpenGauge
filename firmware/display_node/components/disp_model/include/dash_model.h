#pragma once
// A kijelző adatmodellje: a vett telemetriából a megjelenítendő értékek és a kapcsolat állapota.
//
// Mit csinál: dekódolja a vett csomagokat, nyilvántartja az utolsó érvényes értékeket, a
//             fordulatszámot és a sebességet simítja (Smoother), eldönti, hogy az adat friss-e,
//             és vezeti a kapcsolatstatisztikát (LinkStats, csomag/s, átlagos térerő).
// Hogyan:     on_packet() a vételi oldalról, values()/link() a megjelenítő oldalról hívódik;
//             az osztály NEM szálbiztos, a két oldal közé a hívó tesz zárat.
// Miért:      a megjelenítés logikája (mikor "nincs adat", mi számít elavultnak) így hardver és
//             LVGL nélkül, PC-n tesztelhető; a nézetek csak kirajzolják az eredményt.
#include <cstddef>
#include <cstdint>

#include "og_link_stats.h"
#include "og_protocol.h"
#include "smoother.h"

namespace og {

// A megjelenítendő értékek egy adott pillanatban.
struct DashValues {
    bool has_data = false;  // jött-e már érvényes csomag
    bool fresh = false;     // az utolsó csomag nem régebbi DashModel::kStaleMs-nál
    uint32_t age_ms = 0;    // az utolsó érvényes csomag kora
    uint16_t flags = 0;     // az utolsó csomag érvényességi bitjei (kFlag...)
    float rpm = 0.f;        // simított
    float speed_kmh = 0.f;  // simított
    float coolant_c = 0.f;
    float throttle_pct = 0.f;
    float vbat_v = 0.f;
    bool mil_on = false;
    uint8_t dtc_count = 0;
};

// A kapcsolat állapota (a "Kapcsolat" nézethez és a naplóhoz).
struct DashLink {
    uint32_t received = 0, lost = 0, stale = 0, resyncs = 0, invalid = 0;
    float loss_ratio = 0.f;
    uint16_t last_seq = 0;
    uint32_t per_sec = 0;  // az utolsó lezárt másodperc csomagszáma
    int rssi_dbm = 0;      // az utolsó lezárt másodperc átlagos térereje (0, ha nem jött csomag)
};

class DashModel {
public:
    // Ennél régebbi adat már nem számít frissnek (5 kimaradt csomag 10 Hz-en).
    static constexpr uint32_t kStaleMs = 500;

    explicit DashModel(uint32_t sample_period_ms = 100) : rpm_(sample_period_ms), speed_(sample_period_ms) {}

    // Mit csinál: egy vett csomagot dolgoz fel. Igaz, ha érvényes, új telemetria volt.
    // Hogyan:     dekódolás (hossz, verzió, típus), sorszám-ellenőrzés; csak az új csomag
    //             értékei kerülnek a modellbe, a duplikált vagy késve érkezett nem.
    bool on_packet(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms);

    // Mit csinál: lezár egy másodpercet: a csomag/s és az átlagos térerő ebből számolódik.
    void on_second();

    DashValues values(uint32_t now_ms) const;
    DashLink link() const;

private:
    LinkStats stats_;
    TelemetryMsg last_ = {};
    bool has_data_ = false;
    uint32_t last_ms_ = 0;
    Smoother rpm_, speed_;
    uint32_t invalid_ = 0;
    uint32_t sec_count_ = 0, per_sec_ = 0;
    int32_t sec_rssi_sum_ = 0;
    int rssi_avg_ = 0;
};

}  // namespace og
