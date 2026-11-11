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
enum class SystemVoltage : uint8_t { Auto = 0, V12 = 1, V24 = 2 };  // az akkumulátor-skála: a mért feszültségből vagy kényszerítve

constexpr int kMaxScreens = 8;  // a képernyő-sorrend tömbjének mérete
constexpr int kScreenIds = 7;   // jelenleg ismert képernyők: 0 fő, 1 részletek, 2 kapcsolat, 3 zene, 4 mutatós fordulatszámmérő, 5 mutatós sebességmérő, 6 kettős mérő (akkumulátor és hűtővíz)

struct Settings {
    bool music_enabled = true;        // zene képernyő (BLE); kikapcsolva a BLE csak az ébresztéshez kell
    bool debug_mode = true;           // a Részletek és a Kapcsolat képernyő, az FPS és a CPU kijelzése
    uint16_t max_rpm = 8000;          // a fordulatszám-skála vége (az ECU-ból nem olvasható)
    uint16_t redline_rpm = 6500;      // a piros sáv kezdete
    uint16_t max_speed_kmh = 240;     // a sebességskála vége
    uint16_t sleep_s = 0;             // elalvási idő; 0: a fordításkor beállított érték
    uint8_t palette = 0;              // színpaletta azonosítója
    bool skip_logo = false;           // az indító logó kihagyása
    uint8_t brightness_pct = 80;      // alap fényerő (a fordításkori alapérték)
    SpeedUnit speed_unit = SpeedUnit::Kmh;
    TempUnit temp_unit = TempUnit::Celsius;
    bool auto_switch = true;          // automatikus lapozás a képernyők között
    uint16_t switch_period_s = 5;     // lapozás sebessége (másodperc)
    uint8_t screen_order[kMaxScreens] = {0, 1, 2, 3, 4, 5, 6, 0};  // az első kScreenIds elem számít
    SystemVoltage system_voltage = SystemVoltage::Auto;
    uint8_t gear_count = 5;           // a váltó előremeneti fokozatainak száma (a fokozatbecslés ennyi fokozatot tanul)
    uint16_t final_drive_x100 = 0;    // a főhajtás áttétele századokban (0: nincs megadva)
    uint16_t tyre_circ_mm = 0;        // a kerék gördülési kerülete mm-ben (0: nincs megadva)
    uint16_t gear_ratio_x100[8] = {}; // a fokozatok áttétele századokban (0: nincs megadva); mind a gear_count darab kell a képlethez
    uint8_t screen_mask = (1u << kScreenIds) - 1;  // a lapozásban részt vevő képernyők (azonosítónként egy bit); a debug és a zene feltételei ezen felül érvényesek
};

// Mit csinál: az értékeket érvényes tartományba hozza (a tárolt vagy a felhasználó által beírt adat hibás is lehet).
// Hogyan:     a határokat levágja; a redline legfeljebb a max. fordulatszám; a képernyő-sorrend csak
//             akkor marad, ha pontosan egyszer tartalmaz minden ismert képernyőt, különben az alapsorrend áll vissza;
//             a képernyőmaszkban csak ismert képernyők bitje maradhat, és legalább egy képernyőnek láthatónak kell lennie.
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
    if (s.system_voltage != SystemVoltage::V12 && s.system_voltage != SystemVoltage::V24) s.system_voltage = SystemVoltage::Auto;
    s.screen_mask &= static_cast<uint8_t>((1u << kScreenIds) - 1);
    s.gear_count = std::clamp<uint8_t>(s.gear_count, 3, 8);
    auto zero_or = [](uint16_t v, uint16_t lo, uint16_t hi) -> uint16_t { return v == 0 ? 0 : std::clamp<uint16_t>(v, lo, hi); };
    s.final_drive_x100 = zero_or(s.final_drive_x100, 100, 800);   // 1,00..8,00
    s.tyre_circ_mm = zero_or(s.tyre_circ_mm, 1200, 2800);        // 1,2..2,8 m
    for (uint16_t& r : s.gear_ratio_x100) r = zero_or(r, 20, 999);  // 0,20..9,99
    if (s.screen_mask == 0) s.screen_mask = 1;
    bool seen[kScreenIds] = {};
    bool ok = true;
    for (int i = 0; i < kScreenIds; ++i) {
        const uint8_t id = s.screen_order[i];
        if (id >= kScreenIds || seen[id]) { ok = false; break; }
        seen[id] = true;
    }
    if (!ok) for (int i = 0; i < kMaxScreens; ++i) s.screen_order[i] = static_cast<uint8_t>(i < kScreenIds ? i : 0);
}

constexpr size_t kSettingsBlobSizeV1 = 3 + 18 + kMaxScreens + 2;  // az 1. verzió hossza (azonosító és verzió + mezők + sorrend + ellenőrzőösszeg)
constexpr size_t kSettingsBlobSizeV2 = kSettingsBlobSizeV1 + 2;    // a 2. verzió: + rendszerfeszültség és képernyőmaszk
constexpr size_t kSettingsBlobSizeV3 = kSettingsBlobSizeV2 + 1;    // a 3. verzió: + fokozatszám
constexpr size_t kSettingsBlobSize = kSettingsBlobSizeV3 + 2 + 2 + 16;  // a 4. verzió: + főhajtás, kerékkerület, 8 áttétel
constexpr uint8_t kSettingsVersion = 4;

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
    u8(static_cast<uint8_t>(s.system_voltage)); u8(s.screen_mask);
    u8(s.gear_count);
    u16(s.final_drive_x100); u16(s.tyre_circ_mm);
    for (int i = 0; i < 8; ++i) u16(s.gear_ratio_x100[i]);
    u16(settings_detail::checksum(out, n));
    return n;
}

// Mit csinál: visszaolvassa a bájtsort; hibás hossznál, azonosítónál, verziónál vagy összegnél false, és a kimenet alapérték.
// Hogyan:     sikeres olvasás után a sanitize() is lefut, így a kimenet mindig érvényes. A régebbi (rövidebb) verziókat
//             is olvassa: az újabb mezők ilyenkor alapértékűek, így a frissítés nem veszíti el a mentett beállításokat.
inline bool settings_decode(const uint8_t* in, size_t n, Settings& out) {
    out = Settings{};
    if (!in || n < 3 || in[0] != 'O' || in[1] != 'G') return false;
    const uint8_t ver = in[2];
    if (!((ver == 1 && n == kSettingsBlobSizeV1) || (ver == 2 && n == kSettingsBlobSizeV2) || (ver == 3 && n == kSettingsBlobSizeV3) || (ver == 4 && n == kSettingsBlobSize))) return false;
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
    if (ver >= 2) {
        out.system_voltage = static_cast<SystemVoltage>(u8());
        out.screen_mask = u8();
    }
    if (ver >= 3) out.gear_count = u8();
    if (ver >= 4) {
        out.final_drive_x100 = u16(); out.tyre_circ_mm = u16();
        for (int i = 0; i < 8; ++i) out.gear_ratio_x100[i] = u16();
    }
    sanitize(out);
    return true;
}

}  // namespace og
