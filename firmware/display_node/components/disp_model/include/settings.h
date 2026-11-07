// A kijelző felhasználói beállításai: alapértékek, értékhatárok és a tároláshoz használt bájtformátum.
// Hardverfüggetlen (PC-n tesztelhető); az NVS-be írás a main/settings_store.cpp-ben van.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "palette.h"

namespace og {

enum class SpeedUnit : uint8_t { Kmh = 0, Mph = 1 };
enum class TempUnit : uint8_t { Celsius = 0, Fahrenheit = 1 };

constexpr int kMaxScreens = 8;  // a képernyő-sorrend tömbjének mérete
constexpr int kScreenIds = 4;   // jelenleg ismert képernyők: 0 fő, 1 részletek, 2 kapcsolat, 3 zene

struct Settings {
    bool music_enabled = true;        // zene képernyő (BLE); kikapcsolva a BLE csak az ébresztéshez kell
    bool debug_mode = true;           // a Részletek és a Kapcsolat képernyő, az FPS és a CPU kijelzése
    uint16_t max_rpm = 8000;          // a fordulatszám-skála vége (az ECU-ból nem olvasható)
    uint16_t redline_rpm = 6500;      // a piros sáv kezdete
    uint16_t max_speed_kmh = 240;     // a sebességskála vége
    uint16_t sleep_s = 0;             // elalvási idő; 0: a fordításkor beállított érték
    uint8_t palette = 0;              // színpaletta azonosítója
    bool skip_logo = false;           // az indító logó kihagyása
    uint8_t brightness_pct = 100;     // alap fényerő
    SpeedUnit speed_unit = SpeedUnit::Kmh;
    TempUnit temp_unit = TempUnit::Celsius;
    bool auto_switch = true;          // automatikus lapozás a képernyők között
    uint16_t switch_period_s = 5;     // lapozás sebessége (másodperc)
    uint8_t screen_order[kMaxScreens] = {0, 1, 2, 3, 0, 0, 0, 0};  // az első kScreenIds elem számít
};

// Mit csinál: az értékeket érvényes tartományba hozza (a tárolt vagy a felhasználó által beírt adat hibás is lehet).
// Hogyan:     a határokat levágja; a redline legfeljebb a max. fordulatszám; a képernyő-sorrend csak
//             akkor marad, ha pontosan egyszer tartalmaz minden ismert képernyőt, különben az alapsorrend áll vissza.
inline void sanitize(Settings& s) {
    s.max_rpm = std::clamp<uint16_t>(s.max_rpm, 3000, 12000);
    s.redline_rpm = std::clamp<uint16_t>(s.redline_rpm, 2000, s.max_rpm);
    s.max_speed_kmh = std::clamp<uint16_t>(s.max_speed_kmh, 100, 400);
    if (s.sleep_s != 0) s.sleep_s = std::clamp<uint16_t>(s.sleep_s, 10, 3600);
    if (s.palette >= kPaletteCount) s.palette = 0;
    s.brightness_pct = std::clamp<uint8_t>(s.brightness_pct, 5, 100);
    if (s.speed_unit != SpeedUnit::Mph) s.speed_unit = SpeedUnit::Kmh;
    if (s.temp_unit != TempUnit::Fahrenheit) s.temp_unit = TempUnit::Celsius;
    s.switch_period_s = std::clamp<uint16_t>(s.switch_period_s, 3, 300);
    bool seen[kScreenIds] = {};
    bool ok = true;
    for (int i = 0; i < kScreenIds; ++i) {
        const uint8_t id = s.screen_order[i];
        if (id >= kScreenIds || seen[id]) { ok = false; break; }
        seen[id] = true;
    }
    if (!ok) for (int i = 0; i < kMaxScreens; ++i) s.screen_order[i] = static_cast<uint8_t>(i < kScreenIds ? i : 0);
}

constexpr size_t kSettingsBlobSize = 3 + 18 + kMaxScreens + 2;  // azonosító és verzió + mezők + sorrend + ellenőrzőösszeg
constexpr uint8_t kSettingsVersion = 1;

namespace settings_detail {
inline uint16_t checksum(const uint8_t* p, size_t n) {
    uint16_t a = 1, b = 0;  // Fletcher-16
    for (size_t i = 0; i < n; ++i) { a = static_cast<uint16_t>((a + p[i]) % 255); b = static_cast<uint16_t>((b + a) % 255); }
    return static_cast<uint16_t>((b << 8) | a);
}
}  // namespace settings_detail

// Mit csinál: a beállításokat fix hosszú, little-endian bájtsorrá alakítja (kSettingsBlobSize bájt).
// Miért:      saját formátum, nem a struktúra memóriaképe, így a fordító és a mezők sorrendje nem számít;
//             a verziómező miatt a későbbi bővítés felismerhető.
inline size_t settings_encode(const Settings& s, uint8_t* out, size_t cap) {
    if (cap < kSettingsBlobSize) return 0;
    size_t n = 0;
    auto u8 = [&](uint8_t v) { out[n++] = v; };
    auto u16 = [&](uint16_t v) { out[n++] = static_cast<uint8_t>(v); out[n++] = static_cast<uint8_t>(v >> 8); };
    u8('O'); u8('G'); u8(kSettingsVersion);
    u8(s.music_enabled); u8(s.debug_mode);
    u16(s.max_rpm); u16(s.redline_rpm); u16(s.max_speed_kmh); u16(s.sleep_s);
    u8(s.palette); u8(s.skip_logo); u8(s.brightness_pct);
    u8(static_cast<uint8_t>(s.speed_unit)); u8(static_cast<uint8_t>(s.temp_unit));
    u8(s.auto_switch); u16(s.switch_period_s);
    for (int i = 0; i < kMaxScreens; ++i) u8(s.screen_order[i]);
    u16(settings_detail::checksum(out, n));
    return n;
}

// Mit csinál: visszaolvassa a bájtsort; hibás hossznál, azonosítónál, verziónál vagy összegnél false, és a kimenet alapérték.
// Hogyan:     sikeres olvasás után a sanitize() is lefut, így a kimenet mindig érvényes.
inline bool settings_decode(const uint8_t* in, size_t n, Settings& out) {
    out = Settings{};
    if (!in || n != kSettingsBlobSize || in[0] != 'O' || in[1] != 'G' || in[2] != kSettingsVersion) return false;
    const uint16_t want = static_cast<uint16_t>(in[n - 2] | (in[n - 1] << 8));
    if (settings_detail::checksum(in, n - 2) != want) return false;
    size_t p = 3;
    auto u8 = [&]() { return in[p++]; };
    auto u16 = [&]() { const uint16_t v = static_cast<uint16_t>(in[p] | (in[p + 1] << 8)); p += 2; return v; };
    out.music_enabled = u8() != 0; out.debug_mode = u8() != 0;
    out.max_rpm = u16(); out.redline_rpm = u16(); out.max_speed_kmh = u16(); out.sleep_s = u16();
    out.palette = u8(); out.skip_logo = u8() != 0; out.brightness_pct = u8();
    out.speed_unit = static_cast<SpeedUnit>(u8()); out.temp_unit = static_cast<TempUnit>(u8());
    out.auto_switch = u8() != 0; out.switch_period_s = u16();
    for (int i = 0; i < kMaxScreens; ++i) out.screen_order[i] = u8();
    sanitize(out);
    return true;
}

}  // namespace og
