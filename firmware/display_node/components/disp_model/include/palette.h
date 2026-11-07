// A kijelző színpalettái: előre összeállított színkészletek, hardverfüggetlenül (PC-n is fordul).
// A színek 0xRRGGBB értékek; az LVGL-es átalakítás a megjelenítő kódban van.
#pragma once

#include <cstdint>

namespace og {

struct Palette {
    const char* name;
    uint32_t bg;      // képernyő háttere
    uint32_t track;   // műszer sávja, gombok és csíkok alapszíne
    uint32_t text;    // fő szöveg és számok
    uint32_t dim;     // másodlagos szöveg, címkék
    uint32_t accent;  // a fő jelző szín (ív, haladássáv, címek)
    uint32_t warn;    // figyelmeztetés: redline, hiba, nincs adat
};

constexpr int kPaletteCount = 3;

// Mit csinál: visszaadja az azonosítóhoz tartozó palettát; érvénytelen azonosítónál az alapértelmezettet (0).
inline const Palette& palette_get(int id) {
    static const Palette kPalettes[kPaletteCount] = {
        {"Piros-kék", 0x000000, 0x23262E, 0xFFFFFF, 0x9AA3B5, 0x20A8FF, 0xE02020},
        {"Borostyán", 0x000000, 0x303030, 0xFFFFFF, 0x9A9A9A, 0xFF8000, 0xE02020},
        {"Zöld", 0x000000, 0x1F2B22, 0xFFFFFF, 0x9AB0A0, 0x20C060, 0xE02020},
    };
    return kPalettes[(id >= 0 && id < kPaletteCount) ? id : 0];
}

}  // namespace og
