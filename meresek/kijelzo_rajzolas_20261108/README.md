# Kijelző rajzolási idők mérése (2026-11-08)

Cél: megtudni, mi viszi a rajzolási időt az S3 kijelzőegységen (Rev. A NYÁK, COM9), és mennyit javít rajta néhány egyszerű változtatás.

## Módszer
- `CONFIG_OG_DISP_BENCH` (Kconfig): az indító logó után a műszerfal képernyőin körbe lapoz (3 kör), és minden átmenetnél, valamint másodpercenként kiírja a képkockák idejét (`BENCH` sorok).
- A mérőszámok az LVGL eseményeiből jönnek (`REFR_START/READY`, `FLUSH_START/FINISH`, `FLUSH_WAIT_*`): képkocka ideje, a flush-ra várakozás, a flush-hívás ideje; a különbség a rajzolás (CPU).
- A mérés `lvgl_setup.cpp` (`lvgl_take_prof`) és `dash_app.cpp` (`bench_task`) része; a naplók soronként PC-időbélyeggel a `<változat>/s3_dash.txt` fájlokban vannak.
- A `prof/` mappa egy külön futás naplója az LVGL-profilozóval (`CONFIG_LV_USE_PROFILER` + `main/og_prof.cpp`); a kivont időkben a profilozó saját költsége is benne van, ezért csak a sorrend számít. A `profil/` mappa naplójában a rajzolási feladatok típusonkénti idejei (`RAJZ` sorok) vannak; ezekhez az LVGL egyik forrásfájlját ideiglenesen módosítani kellett, a módosítás nincs a repóban.
- A `buf48/` naplója egy sikertelen indulás (a 2 x 48 soros puffer nem fér a belső RAM-ba).

## Eredmények (átlagos képkocka-idő, ms)

| Változat | lapozás | logó felúszás | logó áttűnés | műszerfal (üresjárat) |
|---|---|---|---|---|
| alap (DIO flash, logó FADE_ON) | 28,2 | 29,7 | 55,6 | 8,3 |
| QIO flash | 20,8 | 22,6 | 41,9 | 6,7 |
| QIO + logó átlátszóság-animációval | 21,5 | 6,5 | 6,6 | 6,3 |
| + adat-cache 64 B sor | 21,6 | 6,5 | 6,5 | 6,4 |
| + RGB565_SWAPPED (bájtcsere az LVGL-ben) | 21,2 | 6,4 | 6,6 | 6,8 |
| 2 x 20 soros puffer | 29,1 | 9,4 | 9,4 | 7,0 |
| 2 x 40 soros puffer (jelenlegi) | 21,5 | 6,4 | 6,6 | 6,3 |

- A flush-ra várakozás minden esetben 0,1-0,3 ms: a kijelző-átvitel nem szűk keresztmetszet, a rajzolás a CPU-é.
- `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM`: nem fordul le (az LVGL teljes kódja nem fér az IRAM-ba, a linker hibát ad).
- Belső RAM: indulás után 13 kB szabad (legnagyobb blokk 7,7 kB); az LVGL kupac 78 %-ig telik. Ezért 48 soros puffer nem indul.

## Profil (lapozás, kivont idő)
Az átmenetek idejében a `lv_obj_redraw` (fa bejárása sávonként), a `call_flush_cb` (sávonként kb. 0,5 ms, benne a bájtcsere), az `lv_obj_update_layout` (animációs képkockánként; `EVENT_GET_SELF_SIZE` kb. 2000 hívás), a szövegrajzolás (`lv_draw_unit_draw_letter_internal`, betűnként kb. 25 µs) és a blend vezet. A felirat-rajzolás pixelenként kb. 140 ns (a doboz területére számolva), az ív 4 ns, a kitöltés 1 ns.
A sávméret mérése: 40 helyett 20 soros pufferrel a lapozás 7,6 ms-mal lassabb, vagyis sávonként kb. 1,9 ms állandó költség van (bejárás + flush). 40 sorral egy teljes képernyős képkocka kb. 5 sávot jelent, így az idő kb. fele ez a költség.
