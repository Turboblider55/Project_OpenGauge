# BLE + ESP-NOW hosszú futás zenelejátszás mellett (2026-11-02)

**Elrendezés:** S2 ECU-szimulátor -> CAN -> C3 (OBD-csomópont, Rev. A) -> ESP-NOW -> S3 (kijelző, Rev. A, műszerfal mód + ZENE képernyő).
Az iPhone a futás kezdete előtt csatlakozott az AMS-en keresztül (kötött kapcsolat), a lejátszás végig tartott.
Időtartam: 90 perc (19:23-20:53). Fájlok: `s3_dash.txt` (S3 napló), `c3_obd.txt.gz` (C3 napló, tömörítve).

## Eredmények
| Mutató | Érték |
|---|---|
| ESP-NOW veszteség | 2,34 % a naplózás alatt (1 261 elveszett csomag), 5 perces szakaszonként 0,7-4,1 %, nincs romló tendencia; a kijelző halmozott számlálója 2,14-2,52 % között mozgott (kezdet 2,29 %, vég 2,33 %) |
| Vett csomag (S3) a naplózás alatt | 52 729 (10 Hz mellett kb. 53 990 az elvárt érték), az S3 nem indult újra |
| Szabad belső RAM (S3) | végig 19 kB (min = max), nincs szivárgás |
| RSSI | -32...-9 dBm, átlag -15,3 dBm |
| C3 hibák/újraindulás | 0 |
| BLE-bontás / watchdog-újrapróbálás | nem látható a naplóban (nincs `og_ams` figyelmeztetés) |

## A számlálók értelmezése (helyesbítés)
A kijelző számlálói az indulása óta halmozódnak (a naplózás kezdetén 11 668 vett és 274 elveszett csomag, uptime kb. 5 perc), ezért a mérés értékei a napló első és utolsó állása közötti különbségek. A korábban közölt 64 387 vett csomag a végállás volt (H-54). Az `espnow_gorbe.png` az RSSI-t és az elveszett csomagok halmozott számát mutatja.

## Korlátok
- A telefon a naplózás előtt csatlakozott, ezért a csatlakozás sorai és az AMS-állapot nincs a naplóban; a kapcsolat folyamatos fennállását a futás közbeni megfigyelés (a zene megy) támasztja alá, a napló csak a bontás hiányát igazolja.
- A kijelzőn végén megjelenő szám helyességét a napló nem rögzíti.
- Az 5 %-os célhoz képest a veszteség jóval alacsonyabb (az előző, rövid futások 1,75-2,8 %-ával egyezik).
