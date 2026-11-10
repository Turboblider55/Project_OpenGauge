// Fokozatbecslés a fordulatszámból és a sebességből (hardverfüggetlen, PC-n tesztelhető).
//
// Mit csinál: megállapítja, hány Hz-es motorfordulatszám jut egy km/h sebességre ("áttételi szám", k = fordulat / sebesség),
//             és ebből a bekapcsolt fokozatot becsli. Két módon dolgozik: (1) a felhasználó által megadott áttételekből
//             (főhajtás, kerékkerület, fokozatonkénti áttétel) képlettel számolja a fokozatok k értékét; (2) ha nincs
//             megadva, vezetés közben megtanulja a fokozatok k értékeit.
// Hogyan:     egy fokozat bekapcsolt tengelykapcsolónál állandó k-t ad, ezért csak az "álló" mintákból tanul: legalább
//             kStableSamples egymás utáni mintán belül a k csak kStableTol-lal változhat. A megtanult k értékek
//             "fürtök" (legfeljebb a fokozatszám darab), egy futó átlaggal és számlálóval. Új fürt csak akkor jön létre, ha
//             van hely; ha tele van, egy jelölt fürt gyűjt adatot, és csak akkor cseréli a leggyengébbet, ha nála több
//             bizonyítéka van (kReplaceMargin). A számlálók felülről korlátosak (kMaxCount), az összes elfogadott minta
//             pedig kFreezeSamples-nál lezárja a tanulást (utána csak besorol), így az adatmennyiség korlátos marad.
//             A fokozatot csak akkor adja meg, ha minden fokozat ismert (képlet vagy kész tanulás): részleges tudásnál a
//             fürtök számozása félrevezető lenne (pl. csak a 3. és 4. fokozatot látta, ezeket 1.-nek és 2.-nak hívná).
//             A kimenetet kStableOut egyező minta után váltja (egy zajos minta nem ugrasztja a kijelzőt).
// Miért:      az OBD-II-ben nincs egységes fokozat-PID (a TCU-t csak gyártóspecifikusan lehet lekérdezni).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace og {

constexpr int kMaxGears = 8;

// A felhasználó által megadott áttételek (nem kötelező). Ha mind megvan, a becslés képlettel megy, tanulás nélkül.
struct GearConfig {
    uint8_t gears = 6;                  // a váltó előremeneti fokozatainak száma (a tanulás ennyi fürtre készül el)
    float final_drive = 0;              // főhajtás áttétele (0: nincs megadva)
    float tyre_circ_m = 0;              // a kerék gördülési kerülete méterben (0: nincs megadva)
    float ratio[kMaxGears] = {};        // fokozatonkénti áttétel (0: nincs megadva)

    bool has_formula() const {
        if (final_drive <= 0.f || tyre_circ_m <= 0.f || gears < 1 || gears > kMaxGears) return false;
        for (int i = 0; i < gears; ++i)
            if (ratio[i] <= 0.f) return false;
        return true;
    }
};

class GearModel {
public:
    static constexpr float kMinSpeedClassify = 5.f;    // km/h: ez alatt nincs fokozat (induláskor a tengelykapcsoló csúszik)
    static constexpr float kMinSpeedLearn = 12.f;      // km/h: ez alatt nem tanul (nagy a relatív hiba)
    static constexpr float kMinRpm = 900.f;            // 1/perc: alatta nem tanul (alapjárat, leállás környéke)
    static constexpr int kStableSamples = 8;           // ennyi egymás utáni, egyező k kell egy tanulási mintához
    static constexpr float kStableTol = 0.04f;         // egy stabil szakasz legnagyobb és legkisebb k értékének relatív különbsége (+ 1 km/h kerekítés: 1 / sebesség)
    static constexpr float kMatchTol = 0.06f;          // egy minta ennyire közel esik a fürtjéhez (relatív)
    static constexpr float kFormulaTol = 0.08f;        // képletes becslésnél a megengedett eltérés
    static constexpr int kStableOut = 3;               // ennyi egyező minta után vált a kimenet
    static constexpr uint16_t kMaxCount = 1000;        // egy fürt számlálójának felső korlátja
    static constexpr uint32_t kFreezeSamples = 30000;  // ennyi elfogadott minta után lezárul a tanulás
    static constexpr uint16_t kReplaceMargin = 8;      // a jelöltnek ennyivel több mintája kell, mint a leggyengébb fürtnek

    struct Cluster {
        float k = 0.f;      // a fürt átlagos áttételi száma (1/perc per km/h)
        uint16_t n = 0;     // a fürtbe sorolt tanulási minták száma (korlátos)
    };

    // Mit csinál: beállítja az áttételeket; ha képlet adható, a tanulás kikapcsol (a fürtök megmaradnak, de nem számítanak).
    void set_config(const GearConfig& c) {
        cfg_ = c;
        cfg_.gears = static_cast<uint8_t>(std::clamp<int>(cfg_.gears, 1, kMaxGears));
        out_ = pending_ = 0;
        pend_n_ = 0;
        if (m_ > cfg_.gears) m_ = cfg_.gears;  // kevesebb fokozat: a felesleges (legkisebb k-jú) fürtök elhagyhatók
    }

    // Mit csinál: egy telemetriamintát dolgoz fel (kb. 10 Hz); valid = a fordulatszám és a sebesség friss és érvényes.
    void update(float rpm, float speed_kmh, bool valid) {
        if (!valid || speed_kmh < kMinSpeedClassify || rpm < 500.f) {
            stab_n_ = 0;
            vote(0);
            return;
        }
        const float k = rpm / speed_kmh;
        vote(classify(k));
        if (cfg_.has_formula() || frozen() || speed_kmh < kMinSpeedLearn || rpm < kMinRpm) {
            stab_n_ = 0;
            return;
        }
        // Stabil szakasz: a minták terjedelme (legnagyobb - legkisebb) kStableTol-on belül marad; így a lassan elcsúszó
        // (pl. üresben pörgetett) k nem látszik állandónak, mert a terjedelem az egész szakaszra vonatkozik.
        // A sebesség egész km/h-ra kerekített, ezért kis sebességnél a k relatív bizonytalansága nagyobb: 1 / sebesség.
        const float tol = kStableTol + 1.f / speed_kmh;
        if (stab_n_ > 0 && std::max(stab_max_, k) - std::min(stab_min_, k) <= tol * std::min(stab_min_, k)) {
            stab_min_ = std::min(stab_min_, k);
            stab_max_ = std::max(stab_max_, k);
            stab_sum_ += k;
            ++stab_n_;
        } else {
            stab_min_ = stab_max_ = stab_sum_ = k;
            stab_n_ = 1;
        }
        if (stab_n_ % kStableSamples == 0) learn(stab_sum_ / static_cast<float>(stab_n_));
    }

    // A jelenlegi fokozat (1..gears), 0: nem ismert (álló autó, kuplungnyomás, váltás, részleges tudás).
    int gear() const { return out_; }
    // A tudás teljessége: képletnél mindig; tanulásnál, ha minden fokozat fürtje megvan.
    bool ready() const { return cfg_.has_formula() || m_ >= cfg_.gears; }
    int learned_gears() const { return m_; }
    bool frozen() const { return samples_ >= kFreezeSamples; }
    uint32_t samples() const { return samples_; }
    const Cluster& cluster(int i) const { return c_[i]; }
    const GearConfig& config() const { return cfg_; }

    // Mit csinál: a tanult adatokat törli (pl. másik autó vagy új áttételek).
    void reset_learning() {
        for (Cluster& c : c_) c = Cluster{};
        cand_ = Cluster{};
        m_ = 0;
        samples_ = 0;
        stab_n_ = 0;
        out_ = pending_ = 0;
        pend_n_ = 0;
        dirty_ = true;
    }

    // Mit csinál: igazat ad, ha a tanult adatok megváltoztak a legutóbbi lekérdezés óta (a mentést a hívó időzíti).
    bool take_dirty() {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }

    static constexpr size_t kBlobSize = 3 + 1 + 1 + 4 + kMaxGears * 4 + 2;  // azonosító és verzió + fokozatszám + fürtök száma + minták + fürtök + összeg

    // Mit csinál: a tanult adatot fix hosszú blobba írja (NVS-hez). A k értékét 1/50 pontossággal tárolja (u16).
    size_t encode(uint8_t* out, size_t cap) const {
        if (cap < kBlobSize) return 0;
        size_t n = 0;
        auto u8 = [&](uint8_t v) { out[n++] = v; };
        auto u16 = [&](uint16_t v) { out[n++] = static_cast<uint8_t>(v); out[n++] = static_cast<uint8_t>(v >> 8); };
        u8('G'); u8('M'); u8(1);
        u8(cfg_.gears);
        u8(static_cast<uint8_t>(m_));
        for (int s = 0; s < 4; ++s) u8(static_cast<uint8_t>(samples_ >> (8 * s)));
        for (int i = 0; i < kMaxGears; ++i) {
            u16(i < m_ ? static_cast<uint16_t>(std::min(65535.f, c_[i].k * 50.f + 0.5f)) : 0);
            u16(i < m_ ? c_[i].n : 0);
        }
        u16(checksum(out, n));
        return n;
    }

    // Mit csinál: visszatölti a tanult adatot; hibás hossznál, azonosítónál, összegnél vagy tartalomnál false (a modell változatlan).
    bool decode(const uint8_t* in, size_t n) {
        if (!in || n != kBlobSize || in[0] != 'G' || in[1] != 'M' || in[2] != 1) return false;
        const uint16_t want = static_cast<uint16_t>(in[n - 2] | (in[n - 1] << 8));
        if (checksum(in, n - 2) != want) return false;
        const int gears = in[3], m = in[4];
        if (gears < 1 || gears > kMaxGears || m > gears) return false;
        uint32_t samples = 0;
        for (int s = 0; s < 4; ++s) samples |= static_cast<uint32_t>(in[5 + s]) << (8 * s);
        Cluster c[kMaxGears] = {};
        size_t p = 9;
        for (int i = 0; i < kMaxGears; ++i) {
            const uint16_t kq = static_cast<uint16_t>(in[p] | (in[p + 1] << 8));
            const uint16_t cnt = static_cast<uint16_t>(in[p + 2] | (in[p + 3] << 8));
            p += 4;
            if (i < m) {
                if (kq == 0 || cnt == 0 || cnt > kMaxCount) return false;
                c[i].k = static_cast<float>(kq) / 50.f;
                c[i].n = cnt;
                if (i > 0 && c[i].k >= c[i - 1].k) return false;  // a fürtök k szerint csökkenő sorrendben vannak
            }
        }
        cfg_.gears = static_cast<uint8_t>(gears);
        for (int i = 0; i < kMaxGears; ++i) c_[i] = c[i];
        m_ = m;
        samples_ = samples;
        cand_ = Cluster{};
        stab_n_ = 0;
        out_ = pending_ = 0;
        pend_n_ = 0;
        dirty_ = false;
        return true;
    }

private:
    GearConfig cfg_;
    Cluster c_[kMaxGears];   // k szerint csökkenő sorrendben: a 0. fürt az 1. fokozat
    Cluster cand_;           // jelölt fürt, ha az összes hely foglalt
    int m_ = 0;
    uint32_t samples_ = 0;
    bool dirty_ = false;
    float stab_min_ = 0.f, stab_max_ = 0.f, stab_sum_ = 0.f;  // a jelenlegi stabil szakasz
    int stab_n_ = 0;
    int out_ = 0, pending_ = 0, pend_n_ = 0;

    static uint16_t checksum(const uint8_t* p, size_t n) {
        uint16_t a = 1, b = 0;  // Fletcher-16
        for (size_t i = 0; i < n; ++i) {
            a = static_cast<uint16_t>((a + p[i]) % 255);
            b = static_cast<uint16_t>((b + a) % 255);
        }
        return static_cast<uint16_t>((b << 8) | a);
    }

    static bool close(float a, float b, float tol) { return b > 0.f && std::fabs(a - b) <= tol * b; }

    // Mit csinál: a k értékhez tartozó fokozat (1..gears) vagy 0, ha nincs elég tudás vagy nincs illeszkedő fokozat.
    int classify(float k) const {
        if (cfg_.has_formula()) {
            int best = 0;
            float err = kFormulaTol;
            for (int i = 0; i < cfg_.gears; ++i) {
                const float ki = (1000.f / 60.f / cfg_.tyre_circ_m) * cfg_.ratio[i] * cfg_.final_drive;  // 1/perc per km/h
                const float e = std::fabs(k - ki) / ki;
                if (e < err) { err = e; best = i + 1; }
            }
            return best;
        }
        if (m_ < cfg_.gears) return 0;
        int best = 0;
        float err = kMatchTol;
        for (int i = 0; i < m_; ++i) {
            const float e = std::fabs(k - c_[i].k) / c_[i].k;
            if (e < err) { err = e; best = i + 1; }
        }
        return best;
    }

    // Mit csinál: a kimenetet csak kStableOut egyező szavazat után váltja.
    void vote(int g) {
        if (g == out_) {
            pend_n_ = 0;
            return;
        }
        if (g == pending_) {
            if (++pend_n_ >= kStableOut) { out_ = g; pend_n_ = 0; }
        } else {
            pending_ = g;
            pend_n_ = 1;
        }
    }

    void sort_clusters() {
        std::sort(c_, c_ + m_, [](const Cluster& a, const Cluster& b) { return a.k > b.k; });
    }

    // Mit csinál: egy stabil mintát a legközelebbi fürthöz ad, új fürtöt nyit, vagy a jelölt fürtön gyűjti.
    void learn(float k) {
        ++samples_;
        dirty_ = true;
        int hit = -1;
        float err = kMatchTol;
        for (int i = 0; i < m_; ++i) {
            const float e = std::fabs(k - c_[i].k) / c_[i].k;
            if (e < err) { err = e; hit = i; }
        }
        if (hit >= 0) {
            Cluster& c = c_[hit];
            c.n = static_cast<uint16_t>(std::min<int>(c.n + 1, kMaxCount));
            c.k += (k - c.k) / static_cast<float>(std::min<int>(c.n, 64));
            return;
        }
        if (m_ < cfg_.gears) {
            c_[m_++] = Cluster{k, 1};
            sort_clusters();
            return;
        }
        // Tele van: a jelölt fürt gyűlik, és csak elég bizonyítékkal cseréli a leggyengébbet.
        if (cand_.n > 0 && close(k, cand_.k, kMatchTol)) {
            cand_.n = static_cast<uint16_t>(std::min<int>(cand_.n + 1, kMaxCount));
            cand_.k += (k - cand_.k) / static_cast<float>(std::min<int>(cand_.n, 64));
        } else {
            cand_ = Cluster{k, 1};
        }
        int weakest = 0;
        for (int i = 1; i < m_; ++i)
            if (c_[i].n < c_[weakest].n) weakest = i;
        if (cand_.n >= c_[weakest].n + kReplaceMargin) {
            c_[weakest] = cand_;
            cand_ = Cluster{};
            sort_clusters();
        }
    }
};

}  // namespace og
