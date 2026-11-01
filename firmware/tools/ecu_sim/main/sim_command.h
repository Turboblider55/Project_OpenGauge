#pragma once
// Az ECU-szimulátor konzolparancsainak értelmezése (hardverfüggetlen, PC-n tesztelhető).
//
// Mit csinál: egy beírt sorból parancsot készít: gyújtás be/ki, az automatikus értékváltozás
//             be/ki, egy érték kézi beállítása, súgó, állapot.
// Hogyan:     szóközzel tagolt szavak: "key on", "key off", "auto on", "auto off", "status",
//             "help", illetve "<mező> <szám>" (rpm, speed, coolant, throttle, vbat).
// Miért:      az érték- és animáció-simítás ellenőrzéséhez kézzel kell tudni állítani az ECU
//             értékeit, az alvási tesztekhez pedig a gyújtás ki-be kapcsolását.
#include <cstdlib>
#include <cstring>

namespace og {

enum class SimCmd { None, Help, Status, KeyOn, KeyOff, AutoOn, AutoOff, Set, Bad };

enum class SimField { Rpm, Speed, Coolant, Throttle, Vbat };

struct SimCommand {
    SimCmd cmd = SimCmd::None;
    SimField field = SimField::Rpm;
    float value = 0.f;
};

// Mit csinál: értelmez egy sort. Üres sorra None, értelmezhetetlenre Bad.
inline SimCommand parse_sim_command(const char* line) {
    SimCommand c;
    char buf[48];
    size_t n = 0;
    while (line[n] && n < sizeof buf - 1) {
        buf[n] = line[n];
        ++n;
    }
    buf[n] = 0;
    while (n && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = 0;
    char* w1 = buf;
    while (*w1 == ' ') ++w1;
    if (!*w1) return c;
    char* w2 = w1;
    while (*w2 && *w2 != ' ') ++w2;
    if (*w2) {
        *w2++ = 0;
        while (*w2 == ' ') ++w2;
    }
    if (!strcmp(w1, "help")) {
        c.cmd = SimCmd::Help;
    } else if (!strcmp(w1, "status")) {
        c.cmd = SimCmd::Status;
    } else if (!strcmp(w1, "key")) {
        c.cmd = !strcmp(w2, "on") ? SimCmd::KeyOn : !strcmp(w2, "off") ? SimCmd::KeyOff : SimCmd::Bad;
    } else if (!strcmp(w1, "auto")) {
        c.cmd = !strcmp(w2, "on") ? SimCmd::AutoOn : !strcmp(w2, "off") ? SimCmd::AutoOff : SimCmd::Bad;
    } else {
        static const struct {
            const char* name;
            SimField f;
        } kFields[] = {{"rpm", SimField::Rpm},         {"speed", SimField::Speed}, {"coolant", SimField::Coolant},
                       {"throttle", SimField::Throttle}, {"vbat", SimField::Vbat}};
        c.cmd = SimCmd::Bad;
        for (const auto& e : kFields) {
            if (strcmp(w1, e.name)) continue;
            char* end = nullptr;
            const float v = strtof(w2, &end);
            if (end != w2 && *end == 0) {
                c.cmd = SimCmd::Set;
                c.field = e.f;
                c.value = v;
            }
            break;
        }
    }
    return c;
}

}  // namespace og
