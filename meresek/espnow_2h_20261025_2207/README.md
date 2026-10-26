# ESP-NOW tartósmérés (asztali, kb. 2 óra)

**Időpont:** 2026-10-25 22:07 – 2026-10-26 00:09 (7313 s naplózva)
**Összeállítás:** ESP32-S2 ECU-szimulátor → CAN (500 kbit/s, SN65HVD230 modulok) → ESP32-C3 OBD-egység → ESP-NOW (1. csatorna, szórás, 10 Hz) → ESP32-S3 kijelző (műszerfal mód, 3 váltakozó képernyő).
**Körülmények:** az első kb. 20 percben a két lapot mozgattam és távolabb tettem egymástól, közéjük akadály került; mellettük telefonos Wi-Fi-hotspot futott zavarforrásként. Utána a lapok egy helyben maradtak.

## Fájlok
| Fájl | Tartalom |
|---|---|
| `s3_dash.txt` | a kijelző naplója: másodpercenként csomag/s, RSSI, halmozott vétel- és veszteségszám, sorszám, FPS, szabad RAM |
| `c3_obd_osszesito.txt` | az OBD-egység naplója a CAN-keretsorok nélkül (másodpercenkénti összesítők) |
| `c3_obd_teljes.zip` | az OBD-egység teljes naplója minden CAN-kerettel (kitömörítve kb. 27 MB) |

Minden sor elején a PC időbélyege áll (`firmware/tools/serial_logger.ps1`), utána az eszköz saját naplósora.

## Eredmények
| Mennyiség | Érték |
|---|---|
| Vett csomag (S3) | 73 053 |
| Elveszett csomag | 82 (0,11 %) – mind az első 20 percben (22:08–22:27), a lapok mozgatása közben |
| Veszteség 22:27 után | 0 (1 óra 42 perc) |
| „Nincs adat” (500 ms-nál régebbi adat) | összesen 4 másodperc, mind a mozgatás alatt |
| Elavult / érvénytelen csomag, megtelt sor, adó-újraindulás | 0 |
| Sorszám-átfordulás (65535 → 0) | 23:16:21-kor, veszteség és hibás észlelés nélkül |
| RSSI | −24 … −66 dBm, átlag −45 dBm; akadállyal (22:37–23:00) átlag kb. −54 dBm, 0 veszteség |
| Kijelző | 3–51 FPS (a képernyőtől függően), szabad belső RAM végig 87–88 kB (nincs szivárgás) |
| OBD-egység (C3) | 97 186 fordulatszám-kérés, 0 hiba; 191 494 CAN-adás, 0 újraindítás; ESP-NOW-küldés 0 hiba |

## Korlátok
- Asztali mérés: a jármű utastere (fémkarosszéria, a telefon Bluetooth-a, egyéb 2,4 GHz-es forgalom) eltérő környezet, azt külön kell mérni (M7).
- Szórásos küldés: nincs MAC-szintű nyugta és ismétlés, a fenti veszteség a nyers rádiós veszteség.
- A naplózó a 125 tervezett percből 122-t rögzített (kézi leállítás).
