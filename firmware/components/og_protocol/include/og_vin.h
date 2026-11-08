#pragma once
// VIN-segédfüggvények: karakterellenőrzés, ellenőrző számjegy, márkafelismerés a gyártókódból (WMI)
// és mintaszám-generálás a szimulátorhoz. Hardverfüggetlen, PC-n tesztelhető.
//
// A WMI-tábla a gyakori márkák rövid, nem teljes listája; ismeretlen gyártókódnál a márka -1.
// A táblázat bővítése: egy sor a kWmi-ben (és új márkánál egy név a kBrandNames-ben).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace og {

constexpr size_t kVinLen = 17;

inline constexpr const char* kBrandNames[] = {
    "Opel", "Volkswagen", "Audi", "BMW", "Mercedes-Benz", "Škoda", "SEAT", "Toyota", "Suzuki", "Ford", "Renault",
    "Peugeot", "Citroën", "Fiat", "Honda", "Hyundai", "Kia", "Dacia", "Volvo", "Mazda", "Nissan",
};
constexpr int kBrandCount = sizeof kBrandNames / sizeof kBrandNames[0];

struct VinWmi {
    char wmi[4];
    uint8_t brand;  // a kBrandNames indexe
};

inline constexpr VinWmi kWmi[] = {
    {"W0L", 0},  {"W0V", 0},  {"WVW", 1},  {"WV1", 1},  {"WV2", 1},  {"3VW", 1},  {"WAU", 2},  {"TRU", 2},
    {"WBA", 3},  {"WBS", 3},  {"WBY", 3},  {"WDB", 4},  {"WDC", 4},  {"WDD", 4},  {"WDF", 4},  {"TMB", 5},
    {"VSS", 6},  {"JTD", 7},  {"JTM", 7},  {"SB1", 7},  {"VNK", 7},  {"TSM", 8},  {"JS2", 8},  {"JS3", 8},
    {"WF0", 9},  {"1FA", 9},  {"1FT", 9},  {"VF1", 10}, {"VF3", 11}, {"VF7", 12}, {"ZFA", 13}, {"JHM", 14},
    {"SHH", 14}, {"KMH", 15}, {"TMA", 15}, {"NLH", 15}, {"KNA", 16}, {"KND", 16}, {"U5Y", 16}, {"UU1", 17},
    {"YV1", 18}, {"JMZ", 19}, {"JN1", 20}, {"SJN", 20},
};
constexpr size_t kWmiCount = sizeof kWmi / sizeof kWmi[0];

// Mit csinál: igaz, ha a szöveg pontosan 17 karakter, és mind nagybetű vagy számjegy, I, O, Q nélkül (ISO 3779).
inline bool vin_chars_valid(const char* vin) {
    if (!vin || std::strlen(vin) != kVinLen) return false;
    for (size_t i = 0; i < kVinLen; ++i) {
        const char c = vin[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        if (!ok || c == 'I' || c == 'O' || c == 'Q') return false;
    }
    return true;
}

// Mit csinál: kiszámolja a VIN 9. helyén álló ellenőrző számjegyet (északamerikai szabály: 0-9 vagy X).
// Hogyan:     betű -> szám átváltás, súlyozott összeg, maradék 11-gyel.
// Miért:      a szimulátor érvényes mintaszámot adjon; az európai járműveknél a számjegy nem kötelező.
inline char vin_check_digit(const char* vin) {
    static const int kWeights[kVinLen] = {8, 7, 6, 5, 4, 3, 2, 10, 0, 9, 8, 7, 6, 5, 4, 3, 2};
    static const char kLetters[] = "ABCDEFGHJKLMNPRSTUVWXYZ";
    static const int kLetterValues[] = {1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 7, 9, 2, 3, 4, 5, 6, 7, 8, 9};
    int sum = 0;
    for (size_t i = 0; i < kVinLen; ++i) {
        const char c = vin[i];
        int v = 0;
        if (c >= '0' && c <= '9') {
            v = c - '0';
        } else {
            const char* p = std::strchr(kLetters, c);
            if (p && c != 0) v = kLetterValues[p - kLetters];
        }
        sum += v * kWeights[i];
    }
    const int r = sum % 11;
    return r == 10 ? 'X' : static_cast<char>('0' + r);
}

// Mit csinál: a gyártókódból (első 3 karakter) megadja a márka azonosítóját; ismeretlennél -1.
inline int vin_brand_id(const char* vin) {
    if (!vin || std::strlen(vin) < 3) return -1;
    for (size_t i = 0; i < kWmiCount; ++i)
        if (std::strncmp(vin, kWmi[i].wmi, 3) == 0) return kWmi[i].brand;
    return -1;
}

inline const char* vin_brand_name(int brand) {
    return (brand >= 0 && brand < kBrandCount) ? kBrandNames[brand] : "";
}

// Mit csinál: mintaszámot állít elő a márkához (a szimulátornak), érvényes karakterekkel és ellenőrző számjeggyel.
// Hogyan:     a márka első gyártókódja, a magból képzett jármű-leíró és sorozatszám; a 10. hely az évkód (2026: T).
inline void vin_make_sample(int brand, uint32_t seed, char out[18]) {
    static const char kBody[] = "ABCDEFGHJKLMNPRSTUVWXYZ0123456789";  // I, O, Q nélkül
    const size_t body_n = sizeof kBody - 1;
    const char* wmi = "W0L";
    for (size_t i = 0; i < kWmiCount; ++i)
        if (kWmi[i].brand == brand) { wmi = kWmi[i].wmi; break; }
    std::memcpy(out, wmi, 3);
    uint32_t x = seed * 2654435761u + 12345u;
    auto next = [&]() { x = x * 1664525u + 1013904223u; return x >> 16; };
    for (size_t i = 3; i < 8; ++i) out[i] = kBody[next() % body_n];
    out[8] = '0';  // helyőrző, lent kiszámoljuk
    out[9] = 'T';  // modellév
    out[10] = kBody[next() % body_n];
    for (size_t i = 11; i < kVinLen; ++i) out[i] = static_cast<char>('0' + next() % 10);
    out[8] = vin_check_digit(out);
    out[kVinLen] = 0;
}

}  // namespace og
