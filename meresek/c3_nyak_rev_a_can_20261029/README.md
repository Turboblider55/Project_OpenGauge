# OBD-NYÁK (Rev. A) első élesztése és CAN-teszt (asztali, 2 perc)

**Időpont:** 2026-10-29 20:47:38 – 20:49:38
**Összeállítás:** ESP32-S2 ECU-szimulátor (SN65HVD230 CAN-TTL modul) → CAN, 500 kbit/s → saját OBD-NYÁK, Rev. A (ESP32-C3-WROOM-02, SN65HVD231). A firmware az `obd_node`, a `OG_BOARD_OBD_REV_A` hardverváltozattal (TX IO4, RX IO5, RS IO10 = LOW). Az ESP-NOW szórás az 1. csatornán megy, a kijelző-egység vételét ez a mérés nem vizsgálta.
**Busz-lezárás:** a CAN-TTL modul ellenállása; a NYÁK-on a JP1 nyitva maradt (lásd a hardver-architektúra 2.4. szakaszát).

## Fájlok
| Fájl | Tartalom |
|---|---|
| `c3_obd_osszesito.txt` | az OBD-NYÁK naplója a CAN-keretsorok nélkül: indulás, protokoll-felismerés, másodpercenkénti összesítők |
| `naplok_teljes.zip` | mindkét eszköz teljes naplója minden CAN-kerettel (`c3_obd_nyak.txt`, `s2_ecu_sim.txt`) |

Minden sor elején a PC időbélyege áll (`firmware/tools/serial_logger.ps1`), utána az eszköz saját naplósora.

## Eredmények
| Mennyiség | Érték |
|---|---|
| Flash | 4 MB (esptool `flash_id`), a bootloader is 4 MB-ot jelez |
| Indulás | `boot:0x8 (SPI_FAST_FLASH_BOOT)`, az alkalmazás figyelmeztetés és hiba nélkül indul |
| Protokoll-felismerés | klasszikus J1979, 500 kbit/s, 11 bites azonosítók, 1 ECU (válasz: 0x7E8), kb. 230 ms alatt |
| VIN, Mode 03 | `WTEST12345678901A`, 2 tárolt hibakód (a szimulátor adatai) |
| CAN-forgalom | 2349 adás, 2350 vétel, 1188 fordulatszám-kérés, 0 hiba, 0 újraindítás |
| ESP-NOW | 1188 küldés, 0 sikertelen |
| Figyelmeztetés / hiba a C3-on | 0 |
| A szimulátor oldalán | 1 nyugtázatlan keret a mérés legelején: a soros port megnyitása resetelte a C3-at, ebben a pillanatban nem volt vevő a buszon |

## Megfigyelés: indulás a flashelés után
Az első flashelés után az esptool automatikus resete a lapkát ismét letöltő módba vitte (`boot:0x0 (USB_BOOT)`, `wait usb download`), pedig az IO9-en 3,3 V mérhető. Kézi reset után a program rendben elindult. Részletek: napló, H-30.

## Korlátok
- A mérés rövid (2 perc), és csak a CAN-utat vizsgálja; az ESP-NOW-vétel minőségét a NYÁK-antennákkal külön mérés vizsgálja.
- Asztali busz egy szimulált ECU-val; valós járműben a válaszidő és a busz terhelése eltér (M7).
