#pragma once
// A Waveshare 1,85"-es panel ST77916 init-táblája (forrás és indoklás: a .c fájl fejlécében).
#include <stdint.h>

#include "esp_lcd_st77916.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const st77916_lcd_init_cmd_t og_st77916_waveshare_1_85_init[];
extern const uint16_t og_st77916_waveshare_1_85_init_size;

#ifdef __cplusplus
}
#endif
