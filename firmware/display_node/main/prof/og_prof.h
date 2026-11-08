// Az LVGL-profilozó csatlakozója (LV_PROFILER_INCLUDE): függvényenkénti saját (kivonó) idő gyűjtése.
// Csak mérési módban kerül be (CONFIG_LV_USE_PROFILER); a mérést az og_prof_enable() kapcsolja.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void og_prof_begin(const char* name);
void og_prof_end(void);

#ifdef __cplusplus
}
#endif

#define LV_PROFILER_BEGIN og_prof_begin(__func__)
#define LV_PROFILER_END og_prof_end()
#define LV_PROFILER_BEGIN_TAG(tag) og_prof_begin(tag)
#define LV_PROFILER_END_TAG(tag) og_prof_end()
