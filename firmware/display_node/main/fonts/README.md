# Ékezetes betűkészletek a kijelzőhöz

A beépített LVGL-es Montserrat betűkészletek csak az ASCII jeleket és a `LV_SYMBOL_*` szimbólumokat tartalmazzák, ezért a magyar ékezetes betűk (á, é, í, ó, ö, ő, ú, ü, ű) és a nemzetközi ékezetek hiányoznának. Ez a mappa a hiányzó karakterekből készít kiegészítő betűkészletet.

## Tartalom
| Fájl | Szerep |
|---|---|
| `Montserrat-Medium.ttf` | a forrás betűtípus (ugyanaz, amelyből az LVGL beépített Montserrat betűi készülnek) |
| `generate.ps1` | a generáló szkript |
| `og_font_montserrat_20.c`, `og_font_montserrat_28.c` | a generált betűkészletek (20 és 28 pixeles) |
| `og_fonts.h` | a betűkészletek deklarációja a kódhoz |

## Mit tartalmaznak
- Latin-1 kiegészítés és Latin Extended-A (U+00A0 – U+017F): magyar, német, francia, spanyol, lengyel, cseh, svéd stb. ékezetes betűi.
- Tipográfiai jelek: félkvirtes és kvirtes gondolatjel (U+2013, U+2014), idézőjelek (U+2018, U+2019, U+201C, U+201D), három pont (U+2026), felsorolásjel (U+2022).
- Az ASCII jeleket és a szimbólumokat **nem** tartalmazzák: a betűkészlet tartaléka (`fallback`) a beépített `lv_font_montserrat_20` / `lv_font_montserrat_28`, onnan jönnek. Így nincs duplázott adat, és a gombok szimbólumai (lejátszás, következő stb.) is megmaradnak.
- A 48 pixeles betűkészlet (nagy számok) a beépített marad, mert csak számjegyeket mutat.

## Használat a kódban
```cpp
#include "og_fonts.h"
lv_obj_set_style_text_font(label, &og_font_montserrat_20, 0);
```
A zenecímek, előadók szövegét a `sanitize_text()` (`music_model.h`) teszi megjeleníthetővé: a betűkészletben szereplő karaktereket meghagyja, a többit (például cirill, japán, emoji) `?`-lé cseréli.

## Újragenerálás
Szükséges: Node.js. Az első futáskor az `npx` letölti az `lv_font_conv` 1.5.3 verziót.

```powershell
powershell -ExecutionPolicy Bypass -File generate.ps1
```

Más tartomány vagy méret esetén a `generate.ps1` elején a `$range` és a `foreach` listája módosítandó. Új méret esetén az `og_fonts.h`-t és a `main/CMakeLists.txt` forráslistáját is bővíteni kell, és az adott méretű beépített Montserrat-ot be kell kapcsolni (`CONFIG_LV_FONT_MONTSERRAT_<méret>=y`), mert az a tartalék.

## Méret
A két betűkészlet együtt kb. 64 kB-tal növeli a programot (a flashben tárolódnak, a RAM-ot nem terhelik).

## Licenc
A Montserrat betűtípus az SIL Open Font License 1.1 alatt szabadon felhasználható és terjeszthető (Julieta Ulanovsky és munkatársai). A generált fájlok a betűtípus származékai, ugyanezen licenc alatt.
