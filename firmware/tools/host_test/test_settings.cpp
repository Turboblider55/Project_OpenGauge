// A kijelző beállításainak és színpalettáinak tesztjei: határok, bájtformátum, hibás adat kezelése.
#include <cstring>

#include "mini_test.h"
#include "settings.h"

using namespace og;

static void defaults_are_valid_and_stable_under_sanitize() {
    Settings s;
    Settings t = s;
    sanitize(t);
    CHECK(std::memcmp(&s, &t, sizeof s) == 0);
    CHECK(s.redline_rpm <= s.max_rpm);
}

static void sanitize_clamps_values() {
    Settings s;
    s.max_rpm = 100;
    s.redline_rpm = 60000;
    s.max_speed_kmh = 5000;
    s.sleep_s = 3;
    s.palette = 99;
    s.brightness_pct = 0;
    s.switch_period_s = 0;
    sanitize(s);
    CHECK(s.max_rpm == 3000 && s.redline_rpm == 3000);  // a redline nem lehet a skála vége fölött
    CHECK(s.max_speed_kmh == 400 && s.sleep_s == 10 && s.palette == 0);
    CHECK(s.brightness_pct == 5 && s.switch_period_s == 3);
    s.sleep_s = 0;
    sanitize(s);
    CHECK(s.sleep_s == 0);  // 0: a fordításkor beállított érték
}

static void screen_order_must_be_a_permutation() {
    Settings s;
    s.screen_order[0] = 2; s.screen_order[1] = 0; s.screen_order[2] = 3; s.screen_order[3] = 1;
    sanitize(s);
    CHECK(s.screen_order[0] == 2 && s.screen_order[3] == 1);  // érvényes sorrend megmarad
    s.screen_order[1] = 2;  // kétszer szerepel
    sanitize(s);
    CHECK(s.screen_order[0] == 0 && s.screen_order[1] == 1 && s.screen_order[2] == 2 && s.screen_order[3] == 3);
    s.screen_order[2] = 9;  // ismeretlen képernyő
    sanitize(s);
    CHECK(s.screen_order[2] == 2);
}

static void encode_decode_round_trip() {
    Settings s;
    s.music_enabled = false; s.debug_mode = false; s.max_rpm = 7000; s.redline_rpm = 6200; s.max_speed_kmh = 260;
    s.sleep_s = 120; s.palette = 2; s.skip_logo = true; s.brightness_pct = 60; s.speed_unit = SpeedUnit::Mph;
    s.temp_unit = TempUnit::Fahrenheit; s.auto_switch = false; s.switch_period_s = 45;
    s.screen_order[0] = 3; s.screen_order[1] = 2; s.screen_order[2] = 1; s.screen_order[3] = 0;
    uint8_t buf[kSettingsBlobSize];
    CHECK(settings_encode(s, buf, sizeof buf) == kSettingsBlobSize);
    Settings r;
    CHECK(settings_decode(buf, sizeof buf, r));
    CHECK(std::memcmp(&s, &r, sizeof s) == 0);
}

static void encode_rejects_small_buffer() {
    uint8_t buf[kSettingsBlobSize - 1];
    CHECK(settings_encode(Settings{}, buf, sizeof buf) == 0);
}

static void decode_rejects_damaged_data_and_gives_defaults() {
    Settings s;
    s.max_rpm = 7000;
    uint8_t buf[kSettingsBlobSize];
    settings_encode(s, buf, sizeof buf);
    Settings r;
    CHECK(!settings_decode(buf, sizeof buf - 1, r));      // rövid
    CHECK(r.max_rpm == Settings{}.max_rpm);
    uint8_t bad[kSettingsBlobSize];
    std::memcpy(bad, buf, sizeof buf); bad[10] ^= 0x01;
    CHECK(!settings_decode(bad, sizeof bad, r));          // összeg hibás
    std::memcpy(bad, buf, sizeof buf); bad[2] = 99;
    CHECK(!settings_decode(bad, sizeof bad, r));          // ismeretlen verzió
    std::memcpy(bad, buf, sizeof buf); bad[0] = 'X';
    CHECK(!settings_decode(bad, sizeof bad, r));          // rossz azonosító
    CHECK(!settings_decode(nullptr, 0, r));
}

static void decode_sanitizes_out_of_range_fields() {
    Settings s;
    uint8_t buf[kSettingsBlobSize];
    settings_encode(s, buf, sizeof buf);
    buf[3 + 2] = 0x10; buf[3 + 3] = 0x00;  // max_rpm = 16: túl kicsi
    // az összeget újra kell számolni, különben az összeghiba miatt utasítaná el
    const uint16_t sum = settings_detail::checksum(buf, kSettingsBlobSize - 2);
    buf[kSettingsBlobSize - 2] = static_cast<uint8_t>(sum);
    buf[kSettingsBlobSize - 1] = static_cast<uint8_t>(sum >> 8);
    Settings r;
    CHECK(settings_decode(buf, sizeof buf, r));
    CHECK(r.max_rpm == 3000);
}

static void palettes_are_complete() {
    for (int i = 0; i < kPaletteCount; ++i) {
        const Palette& p = palette_get(i);
        CHECK(p.name != nullptr && p.name[0] != 0);
        CHECK(p.accent != p.bg && p.text != p.bg && p.warn != p.bg);
    }
    CHECK(&palette_get(-1) == &palette_get(0));  // érvénytelen azonosító: az alapértelmezett
    CHECK(&palette_get(kPaletteCount) == &palette_get(0));
}

int main() {
    RUN(defaults_are_valid_and_stable_under_sanitize);
    RUN(sanitize_clamps_values);
    RUN(screen_order_must_be_a_permutation);
    RUN(encode_decode_round_trip);
    RUN(encode_rejects_small_buffer);
    RUN(decode_rejects_damaged_data_and_gives_defaults);
    RUN(decode_sanitizes_out_of_range_fields);
    RUN(palettes_are_complete);
    TEST_MAIN_END();
}
