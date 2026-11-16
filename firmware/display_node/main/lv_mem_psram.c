// Az LVGL memóriakezelője a PSRAM-ban (egyedi lv_malloc megvalósítás, CONFIG_LV_USE_CUSTOM_MALLOC).
//
// Mit csinál: az LVGL minden foglalását (objektumok, stílusok, szövegek, ideiglenes rajzpufferek) a külső PSRAM-ból
//             szolgálja ki, a belső RAM-ot nem érinti.
// Hogyan:     heap_caps_malloc / realloc / free MALLOC_CAP_SPIRAM-mal; a használt és a csúcshasznált méretet
//             számolja, hogy az lv_mem_monitor() (és a MEMPROF napló) az LVGL saját használatát mutassa.
// Miért:      az LVGL beépített kupaca fix méretű tömb a belső RAM-ban (72 kB); a BLE (42 kB) mellett ez a belső RAM nagy
//             részét vitte el, a méretét pedig a csúcshasználat (beállítások megnyitása) szabta meg. A PSRAM bőséges
//             (8 MB), a rajzpuffer (DMA) továbbra is belső RAM-ban van.
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include <stdint.h>

#include "esp_heap_caps.h"

static size_t g_used = 0;      // az LVGL által lefoglalt bájtok
static size_t g_max_used = 0;  // a csúcshasználat

// Mit csinál: a használt méretet változtatja (szálbiztosan), és követi a csúcsot.
static void account(ptrdiff_t delta)
{
    const size_t now = (size_t)((ptrdiff_t)__atomic_add_fetch(&g_used, (size_t)delta, __ATOMIC_RELAXED));
    size_t peak = __atomic_load_n(&g_max_used, __ATOMIC_RELAXED);
    while(now > peak && !__atomic_compare_exchange_n(&g_max_used, &peak, now, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;  // nincs külön készlet, a PSRAM kupac nő
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void * lv_malloc_core(size_t size)
{
    void * p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(p) account((ptrdiff_t)heap_caps_get_allocated_size(p));
    return p;
}

void * lv_realloc_core(void * p, size_t new_size)
{
    const size_t old = p ? heap_caps_get_allocated_size(p) : 0;
    void * q = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(q) account((ptrdiff_t)heap_caps_get_allocated_size(q) - (ptrdiff_t)old);
    return q;
}

void lv_free_core(void * p)
{
    if(!p) return;
    account(-(ptrdiff_t)heap_caps_get_allocated_size(p));
    heap_caps_free(p);
}

// Mit csinál: az LVGL saját használatát adja vissza úgy, mintha a PSRAM kupac lenne a készlet.
void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    const size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t used = __atomic_load_n(&g_used, __ATOMIC_RELAXED);
    lv_memzero(mon_p, sizeof(lv_mem_monitor_t));
    mon_p->total_size = total;
    mon_p->free_size = total > used ? total - used : 0;
    mon_p->free_biggest_size = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    mon_p->max_used = __atomic_load_n(&g_max_used, __ATOMIC_RELAXED);
    mon_p->used_pct = total ? (uint8_t)(used * 100 / total) : 0;
    mon_p->frag_pct = 0;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

#endif /* LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM */
