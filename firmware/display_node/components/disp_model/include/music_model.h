#pragma once
// A "Most játszik" képernyő hardverfüggetlen logikája: ékezetek lecserélése, az eltelt idő becslése, időformátum.
//
// Mit csinál: (1) az UTF-8 szöveget ASCII-ra egyszerűsíti (a beépített Montserrat betűkészletben nincsenek
//             ékezetes betűk); (2) az iPhone ritka frissítései között a lejátszási sebességgel számolja
//             az eltelt időt; (3) a másodperceket "p:mm" alakra írja.
// Miért:      az AMS az eltelt időt csak eseményre (indítás, szünet, léptetés) küldi, a kijelzőnek kell
//             folyamatosan számolnia; a szövegek ékezetes betűit különben a betűkészlet üres négyzetként
//             rajzolná. Mindez PC-n tesztelhető, ezért nem a nézetben van.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace og {

namespace music_detail {
// U+00C0..U+00FF (Latin-1 kiegészítés) ASCII-megfelelői.
inline const char* latin1() { return "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty"; }
// U+0100..U+017F (Latin kiterjesztett-A) ASCII-megfelelői.
inline const char* latin_ext_a() {
    return "AaAaAa" "CcCcCcCc" "DdDd" "EeEeEeEeEe" "GgGgGgGg" "HhHh" "IiIiIiIiIi" "Ii" "Jj" "Kkk" "LlLlLlLlLl"
           "NnNnNnnNn" "OoOoOo" "Oo" "RrRrRr" "SsSsSsSs" "TtTtTt" "UuUuUuUuUuUu" "Ww" "YyY" "ZzZzZz" "s";
}
}  // namespace music_detail

// Mit csinál: az UTF-8 bemenetet ASCII-ra egyszerűsíti: az ékezetes latin betűket az alapbetűjükre cseréli,
//             a tipográfiai idézőjeleket és gondolatjeleket egyszerű változatukra, a többi nem ASCII
//             karaktert '?'-re; a vezérlőkarakterek szóközzé válnak. A kimenet mindig lezárt.
// Hogyan:     a kódpontot kézzel dekódolja (1-4 bájt); hibás bájtsornál a bájtot '?'-re cseréli.
inline void fold_ascii(const char* in, char* out, size_t cap) {
    if (cap == 0) return;
    size_t o = 0;
    auto put = [&](char c) {
        if (o + 1 < cap) out[o++] = c;
    };
    const unsigned char* p = reinterpret_cast<const unsigned char*>(in ? in : "");
    while (*p && o + 1 < cap) {
        uint32_t cp = *p;
        int extra = 0;
        if (cp < 0x80) {
            put(cp < 0x20 || cp == 0x7F ? ' ' : static_cast<char>(cp));
            ++p;
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
            put('?');
            ++p;
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
        if (!ok) {
            put('?');
            continue;
        }
        if (cp >= 0xC0 && cp <= 0xFF) put(music_detail::latin1()[cp - 0xC0]);
        else if (cp >= 0x100 && cp <= 0x17F) put(music_detail::latin_ext_a()[cp - 0x100]);
        else if (cp == 0xA0) put(' ');
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A) put('\'');
        else if (cp == 0x201C || cp == 0x201D || cp == 0x201E) put('"');
        else if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2013 || cp == 0x2014) put('-');
        else put('?');
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
