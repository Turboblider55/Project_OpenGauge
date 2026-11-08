# Indító logók

Az indításkor megjelenő márkalogók forrása és a konvertáló eszköz. A logó 1 s alatt úszik fel, 2 s-ig látszik, majd 1 s alatt átúszik a műszerfalra (`LV_SCREEN_LOAD_ANIM_FADE_ON`). A logót az utoljára mentett VIN márkája választja ki (`og_logos.cpp`); ha a márkához nincs logó, vagy a „logó kihagyása” beállítás be van kapcsolva, a lépés kimarad.

## Tartalom
| Fájl | Szerep |
|---|---|
| `convert_logo.cjs` | az SVG-ből fehér, átlátszósági maszkos (A8) LVGL-képet készít |
| `og_logo_<márka>.c` | a generált képek (márkánként 15–35 kB flash, a RAM-ot nem terheli) |
| `og_logos.h`, `og_logos.cpp` | márka-azonosító → logó leképezés (`logo_for_brand`) |
| `src/` | a letöltött SVG-k helye; **nincs a repóban** (`.gitignore`) |

## Védjegyek és licenc
A márkalogók a tulajdonosaik védjegyei. A [vehiclespecs/brand-logos](https://github.com/vehiclespecs/brand-logos) tár MIT licence csak a tár szerkezetére vonatkozik, a képekre nem; azok „azonosítás és hivatkozás” célra szerepelnek. Ezért az eredeti SVG-fájlok nem kerülnek a repóba, csak a konvertáló szkript és a belőle készült, egyszínűvé egyszerűsített kép. A logó nem jelenti a márkatulajdonos jóváhagyását vagy támogatását.

## Újragenerálás
Szükséges: Node.js. Az SVG-t a tárból kell letölteni a `src/` mappába.

```bash
npm install --no-save --no-package-lock @resvg/resvg-js@2.6.2
node convert_logo.cjs src/opel.svg og_logo_opel.c 200 160
```

Új márka felvétele: a generált `.c` fájlt fel kell venni a `main/CMakeLists.txt` forráslistájába, a szimbólumot pedig az `og_logos.cpp` deklarációi és táblázata közé (a név egyezzen az `og_vin.h` márkanevével).

A színes vagy színátmenetes logók a világosságuk szerint szürkeárnyalatossá, majd maszkká alakulnak (sötét = átlátszatlan). A kép színét a kódban az `image_recolor` stílus adja, tehát a paletta szerint bármilyen lehet.
