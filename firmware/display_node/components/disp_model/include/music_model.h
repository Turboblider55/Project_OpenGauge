#pragma once
// A "Most játszik" képernyő hardverfüggetlen logikája: szöveg-tisztítás, az eltelt idő becslése, időformátum.
//
// Mit csinál: (1) az UTF-8 szöveget megjeleníthetővé teszi (a betűkészletben nem szereplő karakterek '?'-lé
//             válnak); (2) az iPhone ritka frissítései között a lejátszási sebességgel számolja
//             az eltelt időt; (3) a másodperceket "p:mm" alakra írja.
// Miért:      az AMS az eltelt időt csak eseményre (indítás, szünet, léptetés) küldi, a kijelzőnek kell
//             folyamatosan számolnia; a betűkészletben nem szereplő karaktereket különben üres négyzetként
//             rajzolná a kijelző. Mindez PC-n tesztelhető, ezért nem a nézetben van.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace og {

namespace music_detail {
// Igaz, ha a kódpont szerepel a kijelző betűkészletében (main/fonts): ASCII (a beépített Montserrat),
// Latin-1 kiegészítés + Latin kiterjesztett-A, és a megengedett tipográfiai jelek.
inline bool in_font(uint32_t cp) {
    return (cp >= 0x20 && cp <= 0x7E) || (cp >= 0xA0 && cp <= 0x17F) || cp == 0x2013 || cp == 0x2014 || cp == 0x2018 ||
           cp == 0x2019 || cp == 0x201C || cp == 0x201D || cp == 0x2026 || cp == 0x2022;
}
}  // namespace music_detail

// Mit csinál: az UTF-8 bemenetet megjelenítésre alkalmassá teszi: a betűkészletben szereplő karaktereket
//             (ASCII, Latin-1, Latin kiterjesztett-A, tipográfiai jelek) változatlanul hagyja; a kötőjel-
//             és idézőjel-változatokat a betűkészletben szereplő párjukra cseréli; a vezérlőkarakterek
//             szóközzé, a többi (a betűkészletben nincs meg) és a hibás bájtsor '?'-lé válik.
//             A kimenet mindig lezárt UTF-8, és sosem vág félbe egy karaktert.
// Hogyan:     a kódpontot kézzel dekódolja (1-4 bájt), majd 1-3 bájtban újra kódolja.
// Miért:      egy támogatatlan karakter a betűkészletben üres négyzetként jelenne meg; az iPhone bármilyen
//             nyelvű címet küldhet.
inline void sanitize_text(const char* in, char* out, size_t cap) {
    if (cap == 0) return;
    size_t o = 0;
    auto put_cp = [&](uint32_t cp) {
        const size_t n = cp < 0x80 ? 1 : (cp < 0x800 ? 2 : 3);
        if (o + n >= cap) return false;  // nem fér ki: a lezáró nulla helye megmarad
        if (n == 1) {
            out[o++] = static_cast<char>(cp);
        } else if (n == 2) {
            out[o++] = static_cast<char>(0xC0 | (cp >> 6));
            out[o++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out[o++] = static_cast<char>(0xE0 | (cp >> 12));
            out[o++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        return true;
    };
    const unsigned char* p = reinterpret_cast<const unsigned char*>(in ? in : "");
    while (*p) {
        uint32_t cp = *p;
        int extra = 0;
        if (cp < 0x80) {
            ++p;
            if (!put_cp(cp < 0x20 || cp == 0x7F ? ' ' : cp)) break;
            continue;
        } else if ((cp & 0xE0) == 0xC0) {
            cp &= 0x1F;
            extra = 1;
        } else if ((cp & 0xF0) == 0xE0) {
            cp &= 0x0F;
            extra = 2;
        } else if ((cp & 0xF8) == 0xF0) {
            cp &= 0x07;
            extra = 3;
        } else {
            ++p;
            if (!put_cp('?')) break;
            continue;
        }
        ++p;
        bool ok = true;
        for (int i = 0; i < extra; ++i, ++p) {
            if ((*p & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (*p & 0x3F);
        }
        if (!ok) cp = '?';
        else if (cp == 0x201A) cp = 0x2019;                                      // alsó egyszeres idézőjel
        else if (cp == 0x201E) cp = 0x201D;                                      // alsó kettős idézőjel
        else if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012) cp = '-';         // kötőjel-változatok
        else if (cp == 0x2015) cp = 0x2014;                                      // vízszintes vonal
        else if (!music_detail::in_font(cp)) cp = '?';
        if (!put_cp(cp)) break;
    }
    out[o] = 0;
}

// Mit csinál: az eltelt idő most: az utolsó frissítés értékéhez hozzáadja a frissítés óta eltelt időt a
//             lejátszási sebességgel (csak lejátszás közben); a szám hosszára korlátozza.
// Hogyan:     az ms-időbélyegek 32 bites különbségből számolnak, így a túlcsordulás nem zavar.
inline float music_elapsed(float elapsed_s, uint32_t stamp_ms, float rate, bool playing, uint32_t now_ms, float duration_s) {
    float e = elapsed_s;
    if (playing) e += static_cast<float>(static_cast<uint32_t>(now_ms - stamp_ms)) / 1000.0f * rate;
    if (e < 0) e = 0;
    if (duration_s > 0 && e > duration_s) e = duration_s;
    return e;
}

// Mit csinál: a folyamatjelző értéke 0..1 (ismeretlen hossznál 0).
inline float music_progress(float elapsed_s, float duration_s) {
    if (duration_s <= 0) return 0;
    const float p = elapsed_s / duration_s;
    return p < 0 ? 0 : (p > 1 ? 1 : p);
}

// Mit csinál: másodpercekből "p:mm" (egy óra fölött "ó:pp:mm") szöveget ír.
inline void format_time(char* out, size_t cap, float seconds) {
    const uint32_t s = seconds < 0 ? 0 : static_cast<uint32_t>(seconds);
    if (s >= 3600) std::snprintf(out, cap, "%u:%02u:%02u", static_cast<unsigned>(s / 3600), static_cast<unsigned>(s / 60 % 60), static_cast<unsigned>(s % 60));
    else std::snprintf(out, cap, "%u:%02u", static_cast<unsigned>(s / 60), static_cast<unsigned>(s % 60));
}

}  // namespace og
