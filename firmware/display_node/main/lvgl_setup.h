#pragma once
// Az LVGL elindítása a kerek kijelzőn (közös az LVGL-teszthez és a műszerfalhoz).
#include <cstdint>

#include "lcd_panel.h"
#include "lvgl.h"
#include "touch_panel.h"

namespace og {

// Mit csinál: elindítja az esp_lvgl_port-ot (LVGL-feladat az 1. magon) és a panelre köti az
//             LVGL-t részleges frissítéssel, két 40 soros DMA-pufferrel; bekapcsolja a
//             képkockaszámlálót. nullptr, ha nem sikerült.
// Miért:      a 0. mag a rádióé (Wi-Fi, ESP-NOW), az 1. mag a rajzolásé, így egymást nem lassítják.
lv_display_t* lvgl_start(LcdPanel& lcd, bool swap_bytes);

// Mit csinál: az érintőt LVGL-bemenetként a kijelzőhöz köti (gesztusok, nyomás, húzás).
lv_indev_t* lvgl_add_touch(lv_display_t* disp, TouchPanel& touch);

// Az utolsó hívás óta kirajzolt képkockák száma (a számláló nullázódik).
uint32_t lvgl_take_frames();

// Az utolsó lekérdezés óta mért rajzolási idők (csak a ténylegesen rajzolt képkockákra; mikroszekundum).
struct LvglProf {
    uint32_t frames = 0;      // kirajzolt képkockák
    uint32_t chunks = 0;      // a kijelzőre küldött sávok
    uint64_t frame_us = 0;    // képkockák összideje (REFR_START -> REFR_READY)
    uint64_t wait_us = 0;     // a flush befejezésére várakozás (a CPU ilyenkor tétlen)
    uint64_t flush_us = 0;    // a flush-hívások ideje (a DMA indítása)
    uint64_t period_us = 0;   // két rajzolt képkocka kezdete közti idő összege
    uint32_t max_frame_us = 0;
    uint32_t slow_frames = 0; // 33 ms-nál hosszabb képkockák (30 FPS alatt)
};

// Mit csinál: visszaadja és nullázza a rajzolási idők mérőszámait.
// Miért:      megmutatja, hogy a képkocka ideje a rajzolással (CPU/memória) vagy a kijelzőre várással telik.
LvglProf lvgl_take_prof();


}  // namespace og
