// A függvényenkénti idő gyűjtése az LVGL-profilozó hívásaiból (lásd prof/og_prof.h).
#include "og_prof.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

namespace {

struct Entry {
    const char* name;
    int64_t t0;
    int64_t child;  // a belső hívások összideje (a kivonó időhöz)
};
struct Stat {
    const char* name;
    uint32_t calls;
    int64_t excl_us;
};

constexpr int kDepth = 48;
constexpr int kNames = 160;
Entry g_stack[kDepth];
int g_sp = 0;
Stat g_stats[kNames];
int g_n = 0;
volatile bool g_on = false;

Stat* find(const char* name) {
    for (int i = 0; i < g_n; ++i)
        if (g_stats[i].name == name) return &g_stats[i];
    for (int i = 0; i < g_n; ++i)
        if (!std::strcmp(g_stats[i].name, name)) return &g_stats[i];
    if (g_n >= kNames) return nullptr;
    g_stats[g_n] = {name, 0, 0};
    return &g_stats[g_n++];
}

}  // namespace

extern "C" void og_prof_begin(const char* name) {
    if (!g_on || g_sp >= kDepth) return;
    g_stack[g_sp++] = {name, esp_timer_get_time(), 0};
}

extern "C" void og_prof_end(void) {
    if (!g_on || g_sp <= 0) return;
    const Entry e = g_stack[--g_sp];
    const int64_t incl = esp_timer_get_time() - e.t0;
    if (Stat* s = find(e.name)) {
        ++s->calls;
        s->excl_us += incl - e.child;
    }
    if (g_sp > 0) g_stack[g_sp - 1].child += incl;
}

// Mit csinál: be/ki kapcsolja a gyűjtést, és nulláz. Ki: csak a verem ürül.
extern "C" void og_prof_enable(bool on) {
    g_sp = 0;
    g_n = 0;
    g_on = on;
}

// Mit csinál: a legtöbb saját időt használó függvények listáját naplózza (ms, hívásszám), majd nulláz.
extern "C" void og_prof_dump(const char* what) {
    g_on = false;
    for (int round = 0; round < 12 && round < g_n; ++round) {
        int best = round;
        for (int i = round + 1; i < g_n; ++i)
            if (g_stats[i].excl_us > g_stats[best].excl_us) best = i;
        const Stat t = g_stats[round];
        g_stats[round] = g_stats[best];
        g_stats[best] = t;
    }
    char line[600];
    int n = 0;
    for (int i = 0; i < 12 && i < g_n; ++i)
        n += snprintf(line + n, sizeof line - n, " | %s %.1fms/%lux", g_stats[i].name, g_stats[i].excl_us / 1000.0, (unsigned long)g_stats[i].calls);
    ESP_LOGI("og_prof", "PROF %s:%s", what, line);
    g_n = 0;
    g_sp = 0;
}
