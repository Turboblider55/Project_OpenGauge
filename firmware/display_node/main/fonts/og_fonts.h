#pragma once
// A kijelző ékezetes betűkészletei (Latin-1 + Latin Extended-A), a generate.ps1 készíti őket.
//
// Mit csinál: méretenként egy betűkészletet ad, amely a magyar és a nyugat-/közép-európai ékezetes betűket
//             és néhány tipográfiai jelet tartalmaz; az ASCII jelek és a LV_SYMBOL_* szimbólumok a
//             beépített Montserrat betűkészletből jönnek (tartalék, fallback).
// Miért:      a beépített Montserrat csak ASCII-t tartalmaz (lásd README.md).
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t og_font_montserrat_20;
extern const lv_font_t og_font_montserrat_28;

#ifdef __cplusplus
}
#endif
