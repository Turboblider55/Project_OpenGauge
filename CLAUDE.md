# OpenGauge – munkamenet-útmutató

Ez a fájl a repo gyökerébe kerül. Az új munkamenet automatikusan beolvassa.

## Projekt röviden
Szakdolgozati projekt (GAMF, Neumann János Egyetem): két egyedi NYÁK.
- **OBD-csomópont:** ESP32-C3-WROOM-02, TPS54302, SN65HVD231 CAN-adó-vevő. Kiolvassa az OBD-II adatokat, ESP-NOW-n küldi.
- **Kijelző-csomópont:** ESP32-S3-WROOM-1-N8R8, Waveshare 1,85″ 360×360 (ST77916 QSPI + CST816S érintő), LiPo + TP4056.
- Egy közös szakdolgozat készül, a hardveres és a szoftveres rész számozott szakaszokkal hivatkozik egymásra.

## Mappák
| Mappa | Tartalom |
|---|---|
| `firmware/obd_node` | ESP-IDF projekt (esp32c3) |
| `firmware/display_node` | ESP-IDF projekt (esp32s3) |
| `firmware/tools/ecu_sim` | ESP-IDF projekt (esp32s2, ECU-szimulátor) |
| `firmware/tools/host_test` | PC-s CMake tesztek (nem ESP-projekt) |
| `firmware/components` | közös, hardverfüggetlen komponensek |
| `docs/` | dokumentáció; a fejlesztési napló: `fejlesztesi_naplo.xlsx` |

A három ESP-projektet külön-külön kell megnyitni és fordítani (`idf.py set-target <chip>`, `idf.py build`). ESP-IDF: 5.5.x.

PC-s tesztek:
`cmake -S firmware/tools/host_test -B build_host && cmake --build build_host && ctest --test-dir build_host`

## Szabályok
- **Nyelv:** minden szöveg magyarul. A eladóknak, JLCPCB-nek szóló levelek angolul.
- **Kódkommentek:** magyarul; minden függvény fölött rövid leírás: mit csinál / hogyan / miért.
- **Dokumentumok:** .docx + .md formában; PDF nem kell. A repo-dokumentumok szövege általános és időtlen, nem írja le a projekt pillanatnyi állapotát.
- **Őszinteség:** a hibákat és tévedéseket dokumentáljuk (a NYÁK-hibák lehetősége és ténye is bekerül).
- **Emoji:** mértékkel.
- **Napló:** az Excel-naplót helyben szerkeszd (openpyxl), soha ne generáld újra; szerkesztés után futtasd a `recalc.py`-t.
- **Git:** csak kérésre commitolj. A commit üzenet végére:
  `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`
- **Hatókör:** a hibakód-törlés (DTC clear) és az OBDonUDS (J1979-2) kívül esik; az OBDonUDS-t az eszköz csak felismeri és jelzi.
- **Szoftverarchitektúra:** a tiszta logika hardverfüggetlen (`ICanBus` interfész; PC-n `VirtualBus`, hardveren `TwaiBus`). Hardverfüggő pinek: `board_pins.h` hardverváltozatonként.

## Állapot
Az aktuális állapotot a naplóból (Szoftver, Ütemterv, Nyitott kérdések lapok) kell kiolvasni, ne ebből a fájlból.
