# BLE + ESP-NOW hosszú futás zenelejátszás mellett (2026-11-02)

**Elrendezés:** S2 ECU-szimulátor -> CAN -> C3 (OBD-csomópont, Rev. A) -> ESP-NOW -> S3 (kijelző, Rev. A, műszerfal mód + ZENE képernyő).
Az iPhone a futás kezdete előtt csatlakozott az AMS-en keresztül (kötött kapcsolat), a lejátszás végig tartott.
Időtartam: 90 perc (19:23-20:53). Fájlok: `s3_dash.txt` (S3 napló), `c3_obd.txt.gz` (C3 napló, tömörítve).

## Eredmények
| Mutató | Érték |
|---|---|
| ESP-NOW veszteség | 2,25-2,44 %, a futás alatt végig stabil (kezdet 2,29 %, vég 2,33 %), nincs romló tendencia |
| Fogadott csomag | 64 387 (kb. 10/s), az S3 nem indult újra |
| Szabad belső RAM (S3) | végig 19 kB (min = max), nincs szivárgás |
| RSSI | -32...-9 dBm, átlag -15,3 dBm |
| C3 hibák/újraindulás | 0 |
| BLE-bontás / watchdog-újrapróbálás | nem látható a naplóban (nincs `og_ams` figyelmeztetés) |

## Korlátok
- A telefon a naplózás előtt csatlakozott, ezért a csatlakozás sorai és az AMS-állapot nincs a naplóban; a kapcsolat folyamatos fennállását a futás közbeni megfigyelés (a zene megy) támasztja alá, a napló csak a bontás hiányát igazolja.
- A kijelzőn végén megjelenő szám helyességét a napló nem rögzíti.
- Az 5 %-os célhoz képest a veszteség jóval alacsonyabb (az előző, rövid futások 1,75-2,8 %-ával egyezik).
