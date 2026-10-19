# MŰSZAKI LEÍRÁS (ENGINEERING SPECIFICATION)

## ESP32-S3 intelligens kijelző csomópont (Display Node) – kerek érintőkijelző, hibrid tápellátás

| Mező | Érték |
|---|---|
| Dokumentum azonosító | ES-DISP-NODE-001 |
| Verzió / állapot | Dokumentum Rev. A.3 – a gyártásba adott (javított) NYÁK szerint frissítve |
| Dátum | 2026. október 15. |
| Termék | Kör alakú digitális műszerfali kijelző és beviteli modul („Display Node") |
| Kapcsolódó dokumentum | ES-OBD-TLM-001 – OBD-II vezeték nélküli telemetria csomópont (adóoldal) |
| Hatály | A kijelző NYÁK hardvere: mikrovezérlő, grafikus alrendszer, hibrid tápellátás, NYÁK kialakítás |

**Jelölésrendszer.** *[T]* tervezési adat (kapcsolási rajz, alkatrészlista), *[K]* követelmény, *[B]* becslés vagy számított várható érték, amelyet a prototípuson mérni kell. A képletek kódblokkban, tizedesponttal szerepelnek; a folyó szövegben és a táblázatokban tizedesvessző van.

**Hivatkozott dokumentumok:** Espressif ESP32-S3 és ESP32-S3-WROOM-1 adatlap, ESP-IDF (ESP-NOW, BLE, GPIO-ébresztés) dokumentáció; LVGL dokumentáció; Texas Instruments TLV75733 adatlap; TP4056 adatlap; AO3401A és SS14 adatlap; Waveshare 1,85″-es érintős LCD modul (SKU 35079) oldala és wikije, ST77916 kijelzővezérlő és CST816S érintővezérlő adatlapja; USBLC6-2P6 adatlap; USB Type-C specifikáció; JLCPCB 4 rétegű stackup (JLC04161H-7628).

---

# 1. RENDSZERÁTTEKINTÉS ÉS MŰSZAKI SPECIFIKÁCIÓ

## 1.1 Rendeltetés és járműipari beépítési koncepció

A Display Node egy kör alakú, érintésvezérelt digitális kiegészítő műszer, amely a jármű műszerfalába vagy szellőzőrácsba illeszthető kisméretű házba (pod) építhető. Feladata az OBD-II csomópont (ES-OBD-TLM-001) által vezeték nélkül küldött motortelemetria (fordulatszám, sebesség, vízhőfok, töltőnyomás) valós idejű, analóg és digitális műszerórák formájában történő megjelenítése, valamint okostelefonnal létesített Bluetooth LE kapcsolaton keresztüli médiavezérlés.

A beépítési koncepció főbb elemei:

- **Vezeték nélküli adatkapcsolat.** A modul nem csatlakozik a jármű CAN-buszához; az adatokat az OBD-csomóponttól ESP-NOW protokollal kapja, így a kijelző a jármű diagnosztikai portjától független helyen, kábelezés nélkül rögzíthető.
- **Hibrid tápellátás.** Normál esetben USB-C porton (járműbeli USB-aljzat vagy 12 V → 5 V adapter) kapja a tápot; a beépített 1S LiPo cella a töltés mellett tápellátás-puffert is ad, így rövid USB-kimaradás alatt nem indul újra.
- **Mechanika.** A kör alakú NYÁK három M2-es furattal (MH01–MH03) rögzíthető elfordulásmentesen a házban; a hátoldalon sík, alkatrészmentes zóna van a lapos LiPo cella felragasztásához (4.2).

## 1.2 Fő funkciók

| Funkció | Megvalósítás | Kulcsparaméter |
|---|---|---|
| Grafikus megjelenítés | LVGL grafikus motor, ESP32-S3 + 8 MB PSRAM, kerek érintőkijelző (240×240 … 480×480, RGB565) | Kettős teljes keretpuffer a PSRAM-ban |
| Telemetria vétel | ESP-NOW (2,4 GHz), bináris csomagok az OBD-csomóponttól | Késleltetés < 4 ms *[K]* |
| Médiavezérlés | BLE kapcsolat okostelefonhoz: számcím, előadó, lejátszás/szünet (Spotify metaadatok) | A metaadat-forrás a telefon oldali szolgáltatás (kísérő alkalmazás vagy a telefon médiaszolgáltatása) |
| Kapacitív érintés | CST816S vezérlő, I²C és megszakításkivezetés (TP_INT) | Lapozás, menü, médiagombok, érintésre ébredés |
| Hibrid tápellátás | USB-C, 1S LiPo, TP4056 töltő, P-FET alapú veszteségmentes tápútválasztó | I_CHG = 500 mA |

> **Megjegyzés (rádió-együttélés).** Az ESP-NOW és a BLE ugyanazt a 2,4 GHz-es rádiót használja, időosztásban. A BLE kapcsolati események (connection event) késleltethetik az ESP-NOW vételét, ezért a < 4 ms-os késleltetés a BLE-forgalommal terhelt működésben csúcsokban meghaladható. Kezelése: nagyobb BLE kapcsolati intervallum (≥ 30–50 ms) és a vezeték nélküli együttélési (coexistence) prioritás beállítása az ESP-NOW javára; az OBD-csomópont ~ 40 Hz-es adási üteméhez (25 ms) képest az esetleges csomagkiesést a kijelző a következő csomagra való interpolációval fedi.

## 1.3 Összesített villamos specifikáció

| Paraméter | Érték | Megjegyzés |
|---|---|---|
| USB bemenet (VBUS) | 5 V (névleges) | USB-C, D+/D− az ESP32-S3 natív USB-hez *[T]* |
| Akkumulátor | 1S LiPo, 3,0 … 4,2 V (belső PCB-védelemmel) | 1000 mAh *[T]*; tervezett cella: LP803040 (AKYGA), 40,5 × 30 × 8 mm, Molex MX-51021-0200 dugó (1,25 mm) |
| Rendszersín V_SYS | ≈ 4,1 V USB-ről (VBUS − F1 − D5 − D8); ≈ V_BAT akkumulátorról | Az LDO bemenete *[B]* |
| Logikai/rendszersín | +3,3 V (TLV75733) | Max. 1,0 A *[T]* |
| Töltőáram | 500 mA (CC), tölti: TP4056 | R18 = 2,4 kΩ *[T]* |
| Áramfelvétel – készenlét (deep sleep, érintésre ébredés) | ≈ 0,05 … 0,15 mA | ESP32-S3 deep sleep + LDO nyugalmi áram (≈ 25 µA) + CST816S készenlét *[B]* |
| Áramfelvétel – aktív kijelző (ESP-NOW figyelés, BLE, háttérvilágítás) | ≈ 250 mA átlag (3,3 V-on) | Tervezési érték *[T]* |
| Áramfelvétel – RF adatátvitel közben | csúcs ≤ 0,5 A (3,3 V-on) | Wi-Fi/BLE adási impulzusok, az LDO 1 A-es kapacitásán belül *[B]* |
| Terhelés az USB-n töltés közben | ≈ 0,75 A (250 mA terhelés + 500 mA töltés, 5 V-on) | USB-C, CC1/CC2 lehúzás 5,1 kΩ-mal *[T]*; legalább 1 A-es tápforrás szükséges *[B]*; a megrendelt 0,5 A-es F1 PPTC-t ez meghaladja (3.2.4) |
| Üzemidő akkumulátorról | ≈ 3 óra (1000 mAh, ≈ 250 mA, ≈ 85 % hasznosítható) | A használható tartomány ≈ 4,2 … 3,4 V (3.3) *[B]* |
| Töltési idő (1000 mAh cella) | ≈ 2,4 óra | 3.1 szerinti számítás *[T]* |

## 1.4 Rendszerarchitektúra és adatfolyam

```
 OBD Node --(ESP-NOW, 2.4 GHz, 16 B)--> +-------------------+
 Okostelefon <-----(BLE, GATT)--------> |   ESP32-S3 (U1)   |
                                        | LVGL | ESP-NOW|BLE|
 USB-C (J1) --ESD (U3)--D+/D--------> | 8 MB Flash+PSRAM  |
    |                                   +---+-----------+---+
    |                                 QSPI+BL|          |I2C+INT+RST
    |                                  +----v-----------v----+
    |                                  | FPC J2 -> kerek LCD |
    |                                  | QSPI vezérlő IC +   |
    |                                  | CST816S (érintés)   |
    |                                  +---------------------+
    +--> F1 (PPTC) --> D5 (SS14) --> +5V --+--> TP4056 (U4) --> LiPo (J5)
    |      (D2: TVS a VBUS-on)               |
    |                                        +--> D8 (SS14) --+
    |                                                         +--> V_SYS --> U5 --> +3.3 V
    LiPo (BTR+) --> Q1 (AO3401A) ------------------------------+
```

> **[Helykitöltő: 1. Ábra – A Display Node rendszerarchitektúrája és adatfolyama]**
> *(Ide kerül a blokkvázlat: adatforrások, ESP32-S3, kijelző és érintés, tápág, USB és akkumulátor.)*

---

# 2. MIKROVEZÉRLŐ ÉS GRAFIKUS ALRENDSZER (ESP32-S3 + LVGL)

## 2.1 Az ESP32-S3-WROOM-1-N8R8 kiválasztásának mérnöki indoklása

A fővezérlő (U1) az **ESP32-S3-WROOM-1-N8R8**: kétmagos Xtensa LX7 processzor 240 MHz-en, 8 MB Quad SPI Flash és 8 MB Octal SPI PSRAM, integrált 2,4 GHz Wi-Fi (ESP-NOW) és Bluetooth LE. A két mag lehetővé teszi a feladatok szétosztását: az egyik magon a rádiós verem (ESP-NOW, BLE), a másikon az LVGL renderelés és a kijelző-flush futhat, így a rádiós események nem akasztják meg a grafikus szálat.

### 2.1.1 Miért nem elegendő a belső 512 kB SRAM?

Egy RGB565 (16 bit/képpont) keretpuffer mérete:

```
M_frame = W * H * 2 bajt

240 x 240:  115 200 B = 112.5 KiB    -> kettős puffer: 225 KiB
360 x 360:  259 200 B = 253.1 KiB    -> kettős puffer: 506 KiB
480 x 480:  460 800 B = 450.0 KiB    -> kettős puffer: 900 KiB
```

A belső SRAM teljes mérete 512 kB, ennek azonban jelentős része foglalt: a Wi-Fi és BLE verem, a FreeRTOS, az LVGL kupac (heap), a DMA-puffereket és a feladatok vermeit is itt kell elhelyezni. A rádiós vermek együtt tipikusan több száz kB-os belső memóriát igényelnek *[B]*, így már a 240 × 240-es kijelző kettős puffere (225 KiB) is a hasznos SRAM-ot kimeríti, a 360 × 360-asé (506 KiB) pedig gyakorlatilag az egész memóriát megtölti, a 480 × 480-as (900 KiB) pedig el sem fér. Részleges (a képernyő töredékét lefedő) pufferekkel az LVGL belső SRAM-mal is működtethető, de a komplex, animált műszeróra-grafika teljes képernyős frissítése és az átfedő renderelés/flush ilyenkor nem megoldható.

### 2.1.2 A PSRAM szerepe: kettős pufferelés

A 8 MB-os Octal PSRAM egyaránt kényelmesen tartalmaz két teljes keretpuffert (akár 480 × 480-ig: 900 KiB) és az LVGL grafikai erőforrásait (betűtípusok, képek). A kettős pufferelés (double buffering) lényege:

1. Az LVGL az **A** pufferbe renderel, miközben a DMA a **B** puffert tolja ki a kijelző felé.
2. A flush befejeztével a két puffer szerepet cserél.

A kijelző így sosem a félig megrajzolt képkockát olvassa; a renderelés és az adatátvitel átfed, ami a képtörés (tearing) és a villogás fő forrását szünteti meg. A kijelző TE (Tearing Effect) jele az FPC 10. lábán (LCD_TE) az ESP32-S3 GPIO47-re van kivezetve, így a flush a panel függőleges szinkronjához igazítható (ha a panel vezérlője támogatja); enélkül a megoldás szoftveres kettős pufferelésre támaszkodik.

Az átviteli idő a kijelző SPI-busz sebességétől függ:

```
t_frame = (W * H * 16 bit) / f_SPI

240 x 240 @ 80 MHz: 921.6 kbit / 80 Mbit/s = 11.5 ms  -> max. 87 fps
240 x 240 @ 40 MHz: 921.6 kbit / 40 Mbit/s = 23.0 ms  -> max. 43 fps
360 x 360 @ 80 MHz (1 adatvonal): 2.07 Mbit / 80 Mbit/s = 25.9 ms -> max. 38 fps

QSPI (4 adatvonal) @ 80 MHz: t_frame / 4
240 x 240: 2.9 ms (≈ 350 fps)      360 x 360: 6.5 ms (≈ 154 fps)

QSPI @ 40 MHz (a GPIO-mátrixon át elérhető megbízható sebesség, lásd 4.3):
240 x 240: 5.8 ms (≈ 174 fps)      360 x 360: 13.0 ms (≈ 77 fps)
```

A QSPI üzemmód (4 adatvonal) az átviteli időt a negyedére csökkenti, és a 360 × 360-as panelnél is bőséges tartalékot ad a 60 fps-hez. Az LVGL részleges frissítése (csak a módosult terület átvitele) ennél lényegesen kevesebb adatot mozgat, ezért az animált műszerórák a gyakorlatban a 30–60 fps-es célt teljesítik.

> **Megvalósítási megjegyzés.** Az ESP32-S3-WROOM-1-N8R8 modulban a GPIO35–GPIO37 az Octal PSRAM-hoz tartozik, ezért ezek a lábak a kártyán nem használhatók; a kártyán ezek a lábak valóban nincsenek bekötve.

## 2.2 FPC csatlakozó (J2) és sín-architektúra

A kijelző csatlakozója 18-tűs, 0,5 mm osztású, alsó érintkezős FPC aljzat (Hirose FH12-18S-0.5SH(55), LCSC C506792). A kiválasztott kijelző a Waveshare 1,85″-es érintős LCD modulja (SKU 35079): 360 × 360 képpont, IPS, ST77916 kijelzővezérlő QSPI interfésszel, CST816S kapacitív érintővezérlő I²C-n, 55 × 55 mm-es modul (45,68 × 45,68 mm aktív terület). A modul a VCC lábon 3,3 V vagy 5 V tápot fogad, és beépített szintillesztőt tartalmaz. A kártya J2 kiosztása az 1–15. lábon **egyezik** a gyártó 18 pines FPC-kiosztásával (a gyártó oldala a 16–18. lábat nem sorolja fel; a kártyán ezek GND-re vannak kötve, ezt a modul kapcsolási rajzán vagy méréssel kell ellenőrizni *[megerősítendő]*). A modulhoz 200 mm-es, 18 eres, 0,5 mm osztású FFC kábel is tartozik, így a közvetlen csatlakoztatás kábellel lehetséges; a kábel érintkezési oldalát (azonos oldali vagy ellenoldali érintkezők) a bekötés előtt ellenőrizni kell, mert a J2 alsó érintkezős.

| J2 láb | Jel | ESP32-S3 GPIO | Funkció |
|---|---|---|---|
| 1 | +3,3 V | – | Tápfeszültség (logika és kijelző) |
| 2 | LCD_BL | IO42 | Háttérvilágítás (PWM) |
| 3 | GND | – | Földelés |
| 4 | LCD_SCLK | IO41 | QSPI órajel |
| 5 | LCD_IO0 | IO40 | QSPI D0 (MOSI) |
| 6 | LCD_IO1 | IO39 | QSPI D1 |
| 7 | LCD_IO2 | IO38 | QSPI D2 |
| 8 | LCD_RST | IO45 | Kijelző hardveres reset (IO45 strapping láb, lásd megjegyzés) |
| 9 | LCD_CS | IO48 | Chip select (aktív alacsony) |
| 10 | LCD_TE | IO47 | Tearing Effect (függőleges szinkron) |
| 11 | LCD_IO3 | IO21 | QSPI D3 |
| 12 | TP_RST | IO14 | Érintővezérlő reset |
| 13 | TP_SCL | IO13 | Érintő I²C órajel |
| 14 | TP_SDA | IO12 | Érintő I²C adat |
| 15 | TP_INT | IO11 | Érintési megszakítás (RTC-képes láb, ébresztésre alkalmas) |
| 16, 17, 18 | GND | – | Földelés |
| MP | Mechanikai fülek | – | Forrasztható rögzítés, tehermentesítés (nem elektromos) |

**Egyéb GPIO-k.** IO0: BOOT gomb (10 kΩ felhúzás); IO43/IO44: UART0 TX/RX tesztpadok (J3/J4); IO19/IO20: USB D−/D+; IO15/IO16/IO17: piros/zöld/kék felhasználói LED; IO7: akkumulátorfeszültség-mérés (100 kΩ/100 kΩ osztó a BTR+-ról, 10 nF szűrőkondenzátorral, ezért V_BAT/2 ≤ 2,1 V az ADC bemenetén). A töltésjelző LED-eket (D6, D7) közvetlenül a TP4056 CHRG és STDBY kimenetei hajtják 1 kΩ-on át.

> **Megjegyzés (IO45).** Az IO45 az ESP32-S3 VDD_SPI-strapping lába. Az N8R8 modulban a VDD_SPI feszültséget az eFuse rögzíti, ezért az indulási érték nem befolyásolja a működést; a kijelző reset-vonalának felhúzását vagy lehúzását ennek ellenére a panel oldali alapállapottal össze kell vetni.

**Sín-architektúra:**

- **Kijelzővezérlés.** A J2 mind a négy QSPI adatvonalat (IO0…IO3), az órajelet, a chip selectet, a reset-et, a TE-t és a háttérvilágítást az ESP32-S3-hoz vezeti; külön DC (parancs/adat) vonal nincs, mert QSPI módban a parancsok az adatvonalakon mennek. A modul ST77916 vezérlője támogatja a QSPI-t (a GC9A01 és az ST7789V csak egyvonalas SPI-t ismer). Szoftverben az ESP-IDF `esp_lcd` komponense és a Waveshare/Espressif ST77916 illesztőprogram használható; a vezérlő inicializálási sorozatát a gyártó példakódjából kell átvenni. Az LCD-jelek az ESP32-S3 GPIO-mátrixán át csatlakoznak (nem a dedikált IOMUX-lábakra), ezért a megbízható órajel 40 MHz-ig feltételezett; 80 MHz csak méréssel igazolható.
- **Érintésvezérlés.** Különálló I²C sín (TP_SDA, TP_SCL), a CST816S tipikus 7 bites címe 0x15, 400 kHz-ig *[B, adatlap szerint ellenőrizendő]*. **A kártyán nincs külső I²C felhúzó ellenállás.** A Waveshare oldala nem közli, hogy a modul tartalmaz-e I²C felhúzót; ha nem, a sín csak az ESP32-S3 belső (≈ 45 kΩ-os) felhúzójával működik, ami 100 kHz-nél általában elegendő, 400 kHz-nél nem. A modul kapcsolási rajzából vagy méréssel (a SDA/SCL ellenállása a 3,3 V-hoz) ellenőrizendő, és 100 kHz-ről érdemes indulni.
- **Háttérvilágítás.** Az LCD_BL kivezetést az ESP32-S3 LEDC (PWM) perifériája hajtja; a villogásmentes fényerőszabályzáshoz ≥ 1 kHz-es PWM-frekvencia javasolt, 8–10 bites felbontással.

> **[Helykitöltő: 2. Ábra – Kijelző interfész és FPC csatlakozó bekötési rajza]**
> *(Ide kerül a J2 bekötési rajza az ESP32-S3 GPIO-khoz, a pull-up ellenállásokkal és a PWM-vezérléssel.)*

## 2.3 A CST816S érintővezérlő és a megszakítás-vezérelt ébresztés

A CST816S kapacitív érintővezérlő egyujjas koordinátákat és beépített kézmozdulat-felismerést (érintés, hosszú érintés, négyirányú pöccintés) szolgáltat I²C-n keresztül. Alacsony fogyasztású készenléti állapotba automatikusan lép, ha nincs érintés; ebben az állapotban az I²C nem válaszol, a kommunikáció az érintés bekövetkeztét jelző **TP_INT** impulzus után indul (szükség esetén TP_RST impulzussal).

**Ébresztési logika:**

```
AKTÍV (kijelző + LVGL)
   | nincs érintés / adat > T1 (pl. 30 s)
   v
HALVÁNYÍTÁS -> KIJELZŐ KI (háttérvilágítás PWM = 0)
   | nincs aktivitás > T2
   v
ALVÁS (light sleep / deep sleep), RF kikapcsolva
   | TP_INT lefutó él (érintés)   | ESP-NOW/BLE esemény (light sleep)
   v                                      v
ÉBREDÉS -> TP_RST/I2C olvasás -> háttérvilágítás vissza -> AKTÍV
```

Az ESP32-S3 deep sleep módból csak az RTC tartományú GPIO-lábak (GPIO0–GPIO21) képesek ébreszteni, light sleep módban bármely GPIO; a TP_INT jelet ezért RTC-képes lábra kell kötni. Az érintésre ébredés a kijelző-kikapcsolt állapot áramfelvételét a készenléti tartományra (≈ 0,1 mA) csökkenti. Az első érintés a felébresztéshez használt eseményként elfogyasztható (nem kerül továbbadásra a menürendszernek), hogy ne történjen véletlen műveletvégrehajtás.

---

# 3. HIBRID TÁPELLÁTÁS ÉS POWER-PATH VEZÉRLÉS

## 3.1 TP4056 akkumulátortöltő és az R_PROG méretezése

A töltő (U4) **TP4056-42-ESOP8** (LCSC C16581), hűtőpados tokozásban: lineáris töltő, állandó áramú / állandó feszültségű (CC/CV) töltési karakterisztikával, egycellás Li-ion/LiPo cellához, 4,2 V ± 1,5 %-os záró feszültséggel.

**A töltőáram beállítása.** A töltőáramot a PROG kivezetés és a föld közé kötött R_PROG ellenállás határozza meg (a PROG kivezetésen a feszültség CC üzemben 1 V):

```
I_CHG = (V_PROG / R_PROG) * 1200 = 1200 / R_PROG
        [V_PROG = 1 V, R_PROG ohmban, I_CHG amperben]

R18 = 2.4 kOhm:  I_CHG = 1200 / 2400 = 0.50 A = 500 mA
```

| R_PROG | I_CHG |
|---|---|
| 1,2 kΩ | 1000 mA |
| 2,4 kΩ | 500 mA *[T]* |
| 10 kΩ | 120 mA |

A 500 mA-es töltőáram 500–1000 mAh-s cellánál 1C–0,5C töltési ütemet jelent, a legtöbb kisméretű LiPo cella megengedett tartományában.

**A töltési ciklus fázisai:**

| Fázis | Feltétel | Töltőáram / feszültség |
|---|---|---|
| Előtöltés (trickle) | V_BAT < ≈ 2,9 V | ≈ I_CHG / 10 = 50 mA |
| Állandó áram (CC) | 2,9 V < V_BAT < 4,2 V | I_CHG = 500 mA |
| Állandó feszültség (CV) | V_BAT = 4,2 V | Az áram exponenciálisan csökken |
| Töltés vége | I_BAT < I_CHG / 10 = 50 mA | A töltő leáll, STDBY jelzés |
| Újratöltés | V_BAT < ≈ 4,05 V | Új töltési ciklus indul |

```
t_charge ≈ (C_bat / I_CHG) * 1.2 = (1000 mAh / 500 mA) * 1.2 ≈ 2.4 óra
        (1.2: a CV szakasz és a veszteségek tényezője)
```

**Állapotjelzés.** A CHRG (töltés közben aktív, nyitott kollektoros) és STDBY (töltés vége) kivezetések hajtják a LED-eket: D6 (piros) töltés alatt, D7 (zöld) feltöltött állapotban.

**Hőterhelés.** A lineáris töltő a bemenet és az akkumulátor közötti feszültségkülönbséget hővé alakítja: P = (V_USB − V_BAT) · I_CHG, azaz a TP4056 VCC-jén (a D5 utáni +5V hálón) mérhető ≈ 4,4 V-tal számolva 3,7 V-os cellánál (4,4 − 3,7) · 0,5 ≈ 0,35 W, mélyen kisütött cellánál akár ≈ 0,7 W. A túlmelegedést a TP4056 belső hőmérséklet-szabályozása (≈ 120 °C-nál csökkenti az áramot) fogja meg, ezért a tok hűtőpadjának nagy rézfelületre kötése a töltési idő szempontjából is kritikus.

## 3.2 Az automatikus veszteségmentes tápútválasztó (Power-Path)

A tápútválasztó célja, hogy a terhelést (U5 LDO) USB jelenlétében az USB lássa el, az akkumulátort ilyenkor kizárólag a TP4056 töltse, USB hiányában pedig az akkumulátor lássa el veszteségmentesen a rendszert. A bemeneti lánc az USB-C csatlakozó VBUS-a után: **F1** (0,5 A-es PPTC), **D2** (SMAJ5.0A TVS-dióda a VBUS-on) és **D5** (SS14), amely a bemeneti védelmet (F1, D2, C3, C11) elválasztja a +5V hálótól. A power-path három eleme: **Q1 (AO3401A P-FET)**, **D8 (SS14 Schottky)** és az **R17 (100 kΩ-os Gate lehúzó ellenállás)**.

```
 VBUS -> F1 (PPTC) -> D5 (SS14) -> +5V   [a hálón: C10 = 10 uF; D2 = TVS a VBUS-on]
                                      |
        +-----------------+-----------+-----------+------------------+
        |                 |                       |                  |
  TP4056 VCC (U4)   D8 (anód -> katód)       Q1 Gate           R17 (100k)
        |                 |                                          |
   BAT = BTR+          V_SYS --> U5 (LDO) --> +3.3 V                GND
   (LiPo, J5)             ^
                          +-- Q1 Source        (Q1 Drain = BTR+)
```

A Q1 (AO3401A: 1 = Gate, 2 = Source, 3 = Drain) source kivezetése a V_SYS csomóponthoz, a drain az akkumulátor pozitív sarkához (BTR+), a gate a D5 utáni +5V hálózathoz kapcsolódik (R17 = 100 kΩ lehúzással, a hálón C10 = 10 µF). *(A Rev. A NYÁK-on a source/drain bekötése fel volt cserélve; a gyártásba adott, javított NYÁK-on (Y10) a bekötés a fentiek szerinti, lásd 3.2.3.)*

| Üzemállapot | VBUS | Q1 gate/V_GS | Q1 | Terhelés tápja | V_SYS |
|---|---|---|---|---|---|
| USB csatlakoztatva | 5 V | V_G ≈ 4,4 V (VBUS − F1 − D5); V_GS ≈ 4,4 − 4,1 = +0,3 V | Kikapcsolt | USB, F1, D5 és D8 on át | ≈ 4,1 V |
| USB lecsatlakoztatva | 0 V | A gate a 100 kΩ-on át földre húzódik: V_GS ≈ −V_BAT | Bekapcsolt (alacsony R_DS(on)) | Akkumulátor, Q1-en át | ≈ V_BAT − I·R_DS(on) |

**USB jelenlétében.** A D8 nyitóirányban vezet, a V_SYS ≈ 4,1 V (VBUS − F1 − D5 − D8). A Q1 V_GS-e pozitív, a FET zár; a testdióda is zár, mert a V_SYS és az akkumulátorfeszültség különbsége (teljesen feltöltött cellánál ≈ −0,1 V) kisebb a testdióda nyitófeszültségénél (≈ 0,5 V). Nagyobb terhelésnél (V_SYS ≈ 3,8 V) a testdióda részben átveheti a terhelést, és az akkumulátor is segít az ellátásban. Az akkumulátor így a TP4056-on kívül semmit nem lát: a terhelés nem „csíp bele" a töltési áramba, ami a TP4056 töltésbefejezés-érzékelése (I_BAT < I_CHG/10) szempontjából is elengedhetetlen. Ha a terhelés a BAT csomópontra volna kötve, a töltő a terhelési áramot is töltőáramnak érzékelné, és a töltés nem fejeződne be.

**USB kihúzásakor.** A VBUS 0 V-ra esik, a D8 zár, és a V_SYS-t a kondenzátorok kezdik táplálni. A Q1 gate-je a 100 kΩ-on át földre húzódik, a V_GS negatív lesz, a FET bekapcsol, és az akkumulátor a Q1 alacsony R_DS(on)-ján (az adatlap szerint 65 mΩ @ V_GS = −4,5 V, 120 mΩ @ −2,5 V) rákapcsolódik a V_SYS-re. Az ellenállásesés konzervatívan 120 mΩ-mal, 250 mA-nél is csak 30 mV. A gate feszültsége sehol nem haladja meg a VBUS szintjét, így V_GS tartománya +0,3 V … −V_BAT (≈ −4,2 V), ami az AO3401A ±12 V-os határán belül van.

### 3.2.1 Átkapcsolási dinamika

**(a) A Q1 testdiódája.** A P-FET belső testdiódája (anód: drain/akkumulátor, katód: source/V_SYS) abban a pillanatban vezetni kezd, amint a V_SYS az akkumulátorfeszültség alá esik: a diódák kapcsolási ideje nanoszekundumos nagyságrendű (< 10 ns *[T]*), így az akkumulátor gyakorlatilag késleltetés nélkül átveszi a terhelést. A testdióda feszültségesése miatt azonban a V_SYS átmenetileg V_BAT − V_f ≈ 3,7 − 0,65 ≈ 3,05 V-ra áll be, amíg a FET be nem kapcsol.

**(b) A Gate kisütési ideje.** A gate-csomópont a D5 utáni +5V háló, amelyen a C10 (10 µF) is rajta van; a gate-et az R17 (100 kΩ) süti ki. USB kihúzásakor először a C10 töltése a D8-on át a terhelést táplálja (néhány 10 µs alatt a +5V háló a V_SYS + D8 nyitófeszültség szintre, ≈ 3,4 V-ra esik), majd a D8 lezár, és a gate-háló már csak az R17-en át süllyed. A FET akkor kapcsol be, amikor a gate feszültsége a source (≈ 3,05 V) alatt legalább |V_GS(th)| ≈ 1,0 V-tal van, azaz V_G < ≈ 2,05 V *[B]*:

```
tau = R17 * C10 = 100 kOhm * 10 uF = 1.0 s
V_G,0 ≈ 3.4 V (a D8 lezárásakor);   V_G,thr ≈ 3.05 V - 1.0 V = 2.05 V

t_on = tau * ln( V_G,0 / V_G,thr ) = 1.0 s * ln(3.4 / 2.05) ≈ 0.5 s

R17 = 1 kOhm esetén: tau = 10 ms, t_on ≈ 5 ms
```

A FET tehát a megrendelt R17 = 100 kΩ mellett ≈ 0,5 másodpercig zárva marad, és ez alatt a testdióda viszi a terhelést (V_SYS ≈ V_BAT − 0,65 V; ≈ 0,65 V · 0,25 A ≈ 0,16 W a SOT-23 tokban, ami erre az időre tűrhető). Az átmenet akkor marad zavartalan, ha V_BAT − 0,7 V az ESP32-S3 brown-out küszöbe (≈ 2,8 V) fölött marad, azaz V_BAT ≳ 3,55 V; alacsonyabb cellafeszültségnél USB kihúzásakor újraindulás várható, amíg a FET be nem kapcsol. A 3.2.4 szerinti utólagos módosítás (R17 || 1 kΩ) a testdiódás szakaszt ≈ 5 ms-ra rövidíti.

> **Tervezési megjegyzés.** Az ideális kapcsolás szempontjából a gate-csomópontot el kellene választani a VBUS-hálózat kapacitásától (C10). A következő hardververzióban R17-et kisebb értékre kell cserélni, vagy a gate-et külön, kis kapacitású csomóponton (kis Schottky dióda a +5V háló és a gate között) kell lehúzni. A tényleges időállandót a prototípuson oszcilloszkóppal ellenőrizni kell (3. ábra).

### 3.2.2 A pufferkondenzátorok áthidalási ideje (holdup)

A V_SYS csomóponton a bemeneti kondenzátor (C7 = 10 µF), az LDO kimenetén a kimeneti kondenzátor (C8 = 10 µF) tölt be átmeneti tárolóként (a kapcsolási rajz szerint; a 3,3 V soron máshol elhelyezett kondenzátorok ehhez hozzáadódnak; a +5V hálón lévő C10 (10 µF) is táplál a D8-on át, amíg a háló a V_SYS + V_f szint fölött van, ez az alábbi konzervatív számításban nem szerepel). Az áthidalási idő a kondenzátorokban tárolt töltésből adódik:

```
Delta_t = (C_total * Delta_V_max) / I_load

C_total = C7 + C8 = 10 uF + 10 uF = 20 uF
Delta_V_max = 0.5 V  (megengedett esés a brownout előtt)

I_load = 150 mA:  Delta_t = (20e-6 * 0.5) / 0.15 = 66.7 us
I_load = 250 mA:  Delta_t = (20e-6 * 0.5) / 0.25 = 40.0 us
```

A 10 V-os X7R/X5R kerámia kondenzátorok kapacitása a 3–5 V-os egyenfeszültség-előfeszítés hatására kb. a felére csökkenhet; 10 µF effektív kapacitással, 250 mA-nél az áthidalási idő ≈ 20 µs *[B]*.

| Esemény | Idő |
|---|---|
| A testdióda bekapcsolása | < 10 ns |
| A Q1 FET bekapcsolása (R17 = 100 kΩ, C10 = 10 µF a gate-hálón) | ≈ 0,5 s |
| A Q1 FET bekapcsolása R17 = 1 kΩ esetén (a 3.2.4 szerinti módosítás után) | ≈ 5 ms |
| Pufferkondenzátor áthidalás (250 mA, névleges kapacitás) | ≈ 40 µs |
| Pufferkondenzátor áthidalás (250 mA, előfeszítés-csökkentett kapacitás) | ≈ 20 µs |

**Értékelés.** A testdióda azonnali átvétele miatt a tápfeszültség nem szakad meg: a pufferkondenzátorok a V_SYS 4,1 V-ról a (V_BAT − V_f) szintre eséséig és a diódás átvétel alatt adnak tartalékot. A tényleges kockázat a **testdióda-fázis feszültségszintje**: az LDO kimenete ilyenkor V_BAT − 0,65 V − dropout ≈ V_BAT − 0,7 V (250 mA-nél a TLV75733 kiesési feszültsége kb. 50 mV-ra becsülhető *[B]*). 3,7 V-os cellánál ez ≈ 3,0 V, a brownout-küszöb (2,8 V) fölött 0,2 V-os tartalékkal; mélyebben kisütött cellánál (V_BAT ≲ 3,5 V) a tartalék elfogy. A firmware-nek ezért a V_BAT ≈ 3,5 V alatti állapotot alacsony akkumulátor-szintként kell kezelnie (kijelző-fényerő csökkentése, majd lekapcsolás).

> **[Helykitöltő: 3. Ábra – Power-Path kapcsolási séma és átkapcsolási hullámformák]**
> *(Ide kerül a Q1/D8/R kapcsolási rajz, valamint V_BUS, V_G, V_SYS és +3,3 V hullámformái USB kihúzáskor.)*

### 3.2.3 A Rev. A NYÁK-on azonosított bekötési hiba és a javított NYÁK (Y10)

A Rev. A kapcsolási rajzon és NYÁK-on a Q1 2. lába (Source) a BTR+-ra, a 3. lába (Drain) a V_SYS-re volt kötve, vagyis a source és a drain a fenti topológiához képest **fel volt cserélve**. A hiba következményei (a Rev. A kártyán, ha legyártották volna):

- **A testdióda iránya fordított** (V_SYS → BTR+). USB jelenlétében az akkumulátort a D8-on és a testdiódán át is töltheti az USB, a TP4056 áramkorlátozását megkerülve; lemerült cellánál ez nagy áram lehet.
- **USB kihúzásakor nincs testdiódás átvétel.** A rendszert csak a C7 és C8 (20 µF) tartja a FET kinyitásáig, ami újraindulást okozhat.
- Külön, csak akkumulátorról (a csatornán át) vagy csak USB-ről (D8-on át) a kártya működik; a hiba az USB és az akkumulátor közötti átkapcsolásban és a párhuzamos töltési útban jelentkezik.

**Javítás.** A hibát a gyártás megkezdése után, az összeszerelés előtt vettem észre. A javított NYÁK-ot új rendelésként adtam le (Y10), amely a JLCPCB-vel egyeztetve a Y9 NYÁK helyére került az SMT-összeszerelési rendelésben. A két NYÁK-fájl összevetése szerint mind az 50 footprint, azok helye, forgatása és az alkatrészlista változatlan, a kontúr azonos; csak a Q1 2. és 3. padjának hálózata cserélődött (a 2. pad most a V_SYS-re, a 3. a BTR+-ra csatlakozik), a hozzájuk tartozó rézvezetés és egy via áthelyezésével. Az új vezetékek távolsága a szomszédos rézelemektől legalább 0,4 mm. A dead-bug javítás így nem szükséges.

> **Tanulság.** A hiba abból adódott, hogy a kapcsolási rajzon a Q1 szimbólumának lábszáma és a footprint lábkiosztása nem egyezett az alkatrész adatlapjával (AO3401A: 1 = Gate, 2 = Source, 3 = Drain). Gyártás előtt minden aktív alkatrész lábkiosztását az adatlappal és a NYÁK-padekkel (nem csak a kapcsolási rajzzal) kell összevetni.

### 3.2.4 Tervezett utólagos kézi módosítások

A JLCPCB közlése szerint a gyártásba indult rendelésben az alkatrészek már nem módosíthatók, ezért a megrendelt (eredeti) értékekkel készülnek el a kártyák, és az alábbi módosításokat kézzel kell elvégezni. Ajánlott sorrend: F1, R17, C6, D2.

| Alkatrész | Megrendelt | Módosítás | Indok |
|---|---|---|---|
| F1 | 0,5 A-es PPTC (C269104) | 1 A-es PPTC (C70061, 0805, 6 V) | A töltés és az üzem együttes áramfelvétele ≈ 0,75 A meghaladja a 0,5 A-es tartóáramot; a PPTC kioldhat |
| R17 | 100 kΩ | 1 kΩ (C17513, 0805), párhuzamosan a meglévőre forrasztva (eredő ≈ 990 Ω) | A gate kisülési ideje ≈ 0,5 s-ról ≈ 5 ms-ra csökken; mellékhatás: ≈ 4 mA állandó terhelés a +5V hálón USB-üzemben |
| C6 (EN) | 100 nF | 1 µF (C28323, 0805), párhuzamosan a meglévőre forrasztva (eredő ≈ 1,1 µF) | Az EN RC-tag az Espressif ajánlásához (10 kΩ + 1 µF) közelít; megbízhatóbb reset |
| D2 | SMAJ5.0A | SMAJ6.0A (C725382) – opcionális | Kisebb szivárgás a VBUS 5 V-os szintje közelében |

Az R17 és a C6 esetén az eredeti alkatrészt nem kell kiforrasztani: a pótlás a meglévő alkatrész padjaira forrasztva, párhuzamosan kapcsolódik, ezért a módosítás reverzibilis és kockázatmentes. Az F1 cseréje kiforrasztást igényel (vagy az F1-gyel párhuzamosan egy második 0,5 A-es PPTC forrasztható, ami a tartóáramot hozzávetőlegesen megkétszerezi).

## 3.3 TLV75733 LDO: méretezés és termikus disszipáció

Az U5 a **TLV75733PDBV** (SOT-23-5, LCSC C485517) fix 3,3 V-os, ultra-alacsony kiesésű (LDO) szabályzó: 1,0 A-es kimeneti áramkapacitás, kiesési feszültség ≈ 200 mV @ 1 A. Az 1,0 A bőségesen lefedi az ESP32-S3 RF áramcsúcsait (≤ 0,5 A *[B]*) és a háttérvilágítás áramát. Az LDO működésének alsó határa a kiesési feszültségből adódik:

```
V_BAT,min = V_OUT + V_dropout + I * R_DS(on) ≈ 3.3 + 0.05 + 0.025 ≈ 3.38 V
            (I = 250 mA)
```

Ez azt jelenti, hogy akkumulátorról az LDO ≈ 3,4 V-ig tart szabályozást (a cella kapacitásának nagy része hasznosítható); ez alatt a 3,3 V-os sín követi az akkumulátort.

**Disszipáció és hőmérséklet-emelkedés.** A lineáris szabályzó a bemenet–kimenet feszültségkülönbséget alakítja hővé:

```
P_loss = (V_IN - V_OUT) * I_load
T_J = T_A + P_loss * theta_JA      (SOT-23-5: theta_JA ≈ 200 degC/W)
```

| Üzemmód | V_IN | ΔV | I_load | P_loss | ΔT | T_A,max (T_J = 125 °C) |
|---|---|---|---|---|---|---|
| Akkumulátor | 3,7 V | 0,4 V | 250 mA | 0,100 W | 20 °C | ≈ 105 °C |
| USB (F1, D5 és D8 esése együtt ≈ 0,9 V) | ≈ 4,1 V | 0,8 V | 250 mA | 0,200 W | 40 °C | ≈ 85 °C |
| USB, javított réz-hűtés (θ_JA ≈ 130 °C/W *[B]*) | ≈ 4,1 V | 0,8 V | 250 mA | 0,200 W | ≈ 26 °C | ≈ 99 °C |

Az akkumulátoros üzem termikusan kényelmes. USB-ről táplálva a három soros elem (F1, D5, D8) együttes feszültségesése miatt a V_IN csak ≈ 4,1 V, így a veszteség ≈ 0,2 W és a hőmérséklet-emelkedés ≈ 40 °C; a megengedett 125 °C-os átmeneti hőmérséklet ≈ 85 °C környezeti hőmérsékletig tartható, ami a napsütéses műszerfal környezetében is tartalékot ad. A 250 mA a folyamatos terhelésre vonatkozik, ami a legrosszabb eset, a rövid áramimpulzusok az átlagos tokhőmérsékletet alig emelik. Az USB-üzem LDO-hűtése ennek ellenére figyelendő tervezési szempont: a SOT-23-5 tokhoz tartozó kivezetések köré a lehető legnagyobb réz-kiöntés (hővezető viák a pad mellett) javasolt, a firmware pedig USB-üzemben korlátozhatja a háttérvilágítás maximumát. A diódák (D5, D8) a V_IN csökkentésével tudatosan segítik az LDO hőterhelését; kapcsolóüzemű szabályzó csak a következő hardververziónál merülhet fel.

## 3.4 Az akkumulátor negatív sarkának (BTR−) közvetlen földelése

Az akkumulátor-csatlakozó (J5, Molex PicoBlade, 2 pólusú, 1,25 mm osztás) kiosztása: 1. láb BTR+ (3,7–4,2 V), 2. láb BTR− (közvetlenül a rendszer teliföldjén, System GND).

**Indoklás.** A felhasznált külső 1S LiPo cellák gyárilag tartalmaznak beépített védőáramkört (DW01A típusú vezérlő és kettős N-MOSFET), amely a cellához tartozó negatív ágba van sorba kötve (a cella sárga kapton fólia alatti védőpanelén). A védelem tehát a cellán belül, a BTR− kivezetésig érvényesül: túltöltésnél, mélykisütésnél és zárlatnál ez a szakasz megszakítja a negatív ágat, és a NYÁK-nak nincs szüksége saját védelemre. A kártya ezért helytakarékosan közvetlenül földeli a BTR− sarkot az In1 réteg teliföldjére, elhagyva a dedikált DW01A + FS8205 típusú védőáramkört.

**Következmények.** A megoldás kizárólag védelemmel ellátott cellával biztonságos; védelem nélküli cella használata a mélykisülés, a túltöltés és a zárlat elleni védelem teljes hiányát jelenti (tűz- és sérülésveszély). A NYÁK ezen felül nem tartalmaz fordított polaritás elleni védelmet az akkumulátor-csatlakozón, ezért a csatlakozó bekötését az első használat előtt ellenőrizni kell (5. fejezet).

---

# 4. 4 RÉTEGŰ NYÁK KIALAKÍTÁS ÉS TERVEZÉSI KIHÍVÁSOK

## 4.1 Alapadatok

A kártya kör alakú kontúrú, lekerekített peremekkel, három M2-es rögzítőfurattal (MH01, MH02, MH03), amelyek a házba az elfordulásmentes beillesztést szolgálják. A hátoldali akkumulátor-zóna 40,5 × 30 mm-es téglalapja átlóján mérve √(40,5² + 30²) ≈ 50,4 mm, így a kör alakú panel átmérője nem lehet kisebb ennél (a peremtartalékkal együtt a gyakorlatban ≥ 52 mm), ami a 1,85″-es kerek kijelző méretével összhangban van.

## 4.2 Rétegszerkezet (JLCPCB JLC04161H-7628, 1,6 mm)

| Réteg | KiCad név | Funkció | Tipikus vastagság |
|---|---|---|---|
| L1 (Top) | F.Cu | ESP32-S3, FPC csatlakozó (J2), USB-C, LDO, TP4056, felületszerelt passzív elemek | 35 µm |
| Prepreg 7628 | – | Dielektrikum L1–L2 | ≈ 0,21 mm |
| L2 (In1) | In1.Cu | **100%-ban szilárd, vágatlan földsík (Solid GND)** | 15,2 µm |
| Core | – | Magdielektrikum L2–L3 | ≈ 1,065 mm |
| L3 (In2) | In2.Cu | Tápelosztás: +5 V USB, +V_BAT, +3,3 V | 15,2 µm |
| Prepreg 7628 | – | Dielektrikum L3–L4 | ≈ 0,21 mm |
| L4 (Bottom) | B.Cu | Nyomógombok (BOOT1, EN1), akkumulátor-zóna (40,5 × 30 mm, sík, alkatrészmentes keepout) | 35 µm |

*A vastagságok névleges értékek; a JLCPCB aktuális stackup-táblázata az irányadó. A KiCad projekt rétegszerkezete általános (0,1 / 1,24 / 0,1 mm), a gyártott kártya viszont JLC04161H-7628 rétegfelépítésű; az impedancia-számítások ennek vastagságaival készültek.*

A tervezett LP803040 cella 8 mm vastag, ezért a ház mélységébe ezt és a LiPo cellák használat közbeni duzzadására tartalékot (≈ 10 %) kell beszámítani. Az L4 hátoldalon az alkatrészmentes zóna a lapos cella felragasztásához szükséges sík felületet biztosítja; az L4 így nem tartalmaz magas alkatrészeket a cella alatt, a gombok (BOOT1, EN1) a zónán kívül, a firmware-feltöltés és újraindítás kényelmes elérhetőségéhez kerültek.

## 4.3 Nagyfrekvenciás jelintegritás

Az L2 szilárd, vágatlan földsík az L1 jelei alatt 0,21 mm távolságra fut, ezért a jelek visszatérő árama közvetlenül a nyomvonal alatt záródik, minimális hurokterülettel. Ez az alapfeltétele a két legkritikusabb jelcsoportnak:

- **A kijelző SPI/QSPI órajele** (SCLK, 40–80 MHz) és a négy adatvonal: rövid, egy rétegen vezetett nyomvonalak, megszakítás nélküli L2 földsík alatt, a vonalak között megfelelő távolsággal (áthallás elleni szabály: ≥ 3× nyomvonalszélesség). A kártyán az útvonalak ≈ 20,8–21,0 mm hosszúak, az adat- és órajelvonalak közötti hosszeltérés legfeljebb ≈ 0,2 mm *[T]*.
- **Az USB D+/D− differenciális pár** (ESP32-S3 natív USB, full-speed 12 Mbit/s): szimmetrikus, egyenlő hosszúságú vezetés az L2 földsík felett, a pár szimmetrikus via-átvitelével, nem tartalmaz csonkot. A célimpedancia 90 Ω; az élcsatolt mikroszalag közelítő képletével:

```
Z0 ≈ 87 / sqrt(er + 1.41) * ln( 5.98*h / (0.8*w + t) )
     (er ≈ 4.4, h = 0.21 mm, t = 0.035 mm)

w = 0.25 mm:  Z0 ≈ 60 Ohm
Z_diff ≈ 2 * Z0 * (1 - 0.48 * exp(-0.96 * s/h)),  s = 0.15 mm
       ->  Z_diff ≈ 92 Ohm
```

**SPI/QSPI útvonal-ellenőrzés.** A jelterjedési késleltetés mikroszalagon t_pd ≈ √ε_eff / c ≈ 6 ps/mm *[B]*:

```
t_vonal = 21.5 mm * 6 ps/mm ≈ 130 ps
Skew    = 0.25 mm * 6 ps/mm ≈ 1.5 ps    (periódus 80 MHz-en: 12.5 ns)
Elektromosan hosszú, ha t_vonal > t_r / 6
   (t_r = 1...2 ns esetén a határ 0.17...0.33 ns)
```

A 0,25 mm-es hosszeltérés időben ≈ 1,5 ps, ami a 12,5 ns-os periódushoz képest elhanyagolható. A ≈ 130 ps-os késleltetés a feszültségélek (1–2 ns) 1/6-a körül van, tehát az útvonal határeset: soros lezárás nélkül, rövid és egyenes vezetéssel általában működik, de az FPC és a panel további vezetékhossza (néhány cm) növeli a visszaverődés kockázatát. A kártyán soros lezárás nincs; az útvonal rövid (≈ 21 mm), ezért 40 MHz-en működőképesnek várható. Az LCD-jelek a GPIO-mátrixon át vannak kötve (nem a dedikált IOMUX-lábakon), ezért a megbízható órajel 40 MHz-ig feltételezett *[ellenőrizendő az ESP-IDF dokumentációban]*; a 80 MHz csak méréssel igazolható. Ha a jelminőség nem megfelelő, a GPIO meghajtóerősségének csökkentése és az órajel mérséklése kompenzálhat.

Az USB differenciális pár célimpedanciájának becslése: a kapott érték (w ≈ 0,25 mm, s ≈ 0,15 mm) tájékoztató; a végleges szélességet a JLCPCB impedanciaszámítója határozza meg. A USB-C csatlakozó (J1) után az USBLC6-2P6 (U3) ESD-védődióda a csatlakozóhoz a lehető legközelebb kerül, a D+/D− vonalak a védelmen keresztül futnak. A kártyán a pár hossza ≈ 61 mm, a két ág hosszeltérése ≈ 0,4 mm; full-speed (12 Mbit/s) üzemhez ez bőven megfelelő.

> **Megjegyzés.** Az ESP32-S3-WROOM-1 modul belső Flash- és PSRAM-buszai a modulon belül futnak, ezért a NYÁK-on nem kell vezetni őket. A kártya nagyfrekvenciás jelei a kijelző SPI-busza és az USB-pár.

## 4.4 Tervezési problémák és megoldások

| Probléma | Megoldás |
|---|---|
| **Via-in-pad.** A forrasztópárnába fúrt via elszívja a forrasztóónt, hidegforrasz és a kis passzívok felbillenése (tombstoning) következik | A jelkivezetések és a kis passzív elemek padjaiba nem kerül via, azok a pad mellé, rövid nyomvonallal vannak kivezetve. Kivétel a két nagy hűtő-/földpad (U4 TP4056 hűtőpad, U1 GND pad 41), amelyek hővezetési viákat tartalmaznak a padben; itt a forrasztóón részleges elszívódása és üregképződés lehetséges, ezért az összeszerelés után a forrasztás minősége (pl. röntgen vagy a töltési hőmérséklet figyelése) ellenőrizendő |
| **Nagy integrációs sűrűség.** A kis kör alapterület miatt a tápútválasztó MOSFET és a töltővezérlő szorosan az FPC csatlakozó alá került | Szoros, rétegek közötti elrendezés: az L1 komponensei, a tápelosztás az L3-on, a hátoldal az akkumulátor-zónának fenntartva |
| **Hátoldali akkumulátor-zóna.** A 40,5 × 30 mm-es sík, alkatrészmentes felület a lapos cella felragasztásához | L4 keepout; a gombok és minden hátoldali alkatrész a zónán kívül |
| **EVQP2 nyomógombok 5. mechanikai füle.** A JLCPCB gyártás-ellenőrzés (DFM) lebegő mechanikai lábat jelzett | A láb elektromosan inaktív, a forrasztásgátló maszkra fekszik fel; a DFM jelzést a gyártási megrendelésnél jóvá kell hagyni (jegyzettel). Alternatíva: földre kötött mechanikai pad hozzáadása |
| **BTR− közvetlen földelése.** A külső védelem hiánya miatt a negatív sarok közvetlenül az In1 teliföldjére kötött | A cellának kötelezően beépített védőáramkörrel kell rendelkeznie (3.4 és 5. fejezet) |
| **Q1 (AO3401A) source/drain felcserélve** a Rev. A kapcsolási rajzon és NYÁK-on | Javítva a gyártásba adott NYÁK-on (Y10): a 2. és 3. pad hálózata cserélve, lábkép és pozíció változatlan (3.2.3) |
| **DRC-maradékok** (a 2026-10-05-i DRC-jelentés alapján) | U1 antenna-keepout: a modul ≈ 6 mm-rel túllóg a kártyaélen, a jelzett hiba a modul saját átfedése (a tiltott zónában nincs réz). J1 NPTH furat és GND pad távolsága 0,194 mm (beállított minimum 0,25 mm); a kártyaél–réz távolság J1 shield padjainál 0,44 mm, az U1 40-es padjánál 0,35 mm (a JLCPCB minimuma fölött) |
| **Edge rail (peremsín)** | Az SMT-összeszereléshez a JLCPCB peremsínt (≥ 5 mm) ad a kártya köré. Az új NYÁK-rendelésnél (Y10) ez utólag, felárral (18 EUR) került hozzáadásra, mert a NYÁK külön rendelésként ment, nem az összeszerelési rendelés részeként |

> **[Helykitöltő: 4. Ábra – Rétegek elrendezése és 3D panel nézet]**
> *(Ide kerül a négy réteg nézete (L1–L4) és a 3D modell elő- és hátoldali képe, az akkumulátor-zóna jelölésével.)*

---

# 5. FELHASZNÁLÓI ÉS BEÜZEMELÉSI ÚTMUTATÓ (MIRE FIGYELJEN A FELHASZNÁLÓ?)

## 5.1 Kereskedelmi LiPo akkumulátorok bekötési kockázata

A kereskedelmi LiPo cellák csatlakozóinak vezetéksorrendje (piros/fekete) **gyártónként eltérhet**, még azonos csatlakozótípusnál (Molex PicoBlade 1,25 mm) is. A kártyán fordított polaritás elleni védelem nincs, ezért a hibás bekötés a töltőt, az LDO-t és az ESP32-S3-at is tönkreteheti, a cellát pedig túláramnak teheti ki.

Az első csatlakoztatás előtt kötelező:

1. **Polaritás mérése multiméterrel.** A cella csatlakozóján meg kell mérni, hogy a piros vezeték a pozitív sarok-e, és hogy az a kártya BTR+ (J5, 1. láb) padjához csatlakozik-e.
2. **Bekötés összevetése.** A J5 1. lába BTR+, a 2. lába BTR− (rendszerföld).
3. **Érintkezők cseréje.** Eltérő kiosztás esetén a tűk a műanyag házból a biztosítófül felpattintásával (vékony tű vagy csipesz segítségével) kivehetők és a helyes sorrendben visszahelyezhetők; a vezetékek kicserélése a csatlakozó forrasztása nélkül elvégezhető.

## 5.2 Belső védőelektronika követelménye

Csak olyan 1S LiPo cella használható, amely **gyárilag rendelkezik belső védőáramkörrel** (túltöltés-, mélykisülés- és zárlatvédelem; jellemzően DW01A + kettős MOSFET a cella védőpanelén, a sárga kapton fólia alatt), mert a NYÁK helyhiány miatt nem tartalmaz dedikált védő IC-t (3.4). Védelem nélküli, „nyers" cella használata tilos. A tervezett cella (LP803040, 1000 mAh, Molex MX-51021-0200 dugóval) a gyártói adatok szerint PCB-védelemmel rendelkezik. A cella tulajdonságait (védelem megléte, névleges kapacitás, megengedett töltőáram ≥ 500 mA) más típusnál a cella adatlapjáról vagy jelöléséről kell ellenőrizni; a dugó vezetékkiosztását típustól függetlenül mérni kell (5.1).

A LiPo cella hőérzékeny: töltése csak mérsékelt hőmérsékleten (tipikusan 0 … 45 °C) megengedett, tartós tárolása és használata magas hőmérsékleten (napsütötte utastér) károsítja és veszélyes lehet. A modult közvetlen napsütéstől védeni kell.

## 5.3 Első feszültség alá helyezés és USB-C használat

**Az első bekapcsolás lépései:**

1. **A Q1 bekötésének ellenőrzése.** A javított NYÁK-on a Q1 2. padja a V_SYS-re (D8 katódja), a 3. padja a BTR+-ra csatlakozik; ezt az első feszültség alá helyezés előtt multiméterrel (folytonosság) ellenőrizni kell.
2. **Zárlat- és polaritásellenőrzés.** A cella csatlakoztatása előtt a polaritás mérése (5.1), valamint a +3,3 V és V_SYS csomópont földhöz mért ellenállásának ellenőrzése (nem lehet rövidzár).
3. **Csatlakoztatás sorrendje.** Először az akkumulátor, utána az USB-C kábel; a töltés LED-jeit (piros: töltés, zöld: feltöltve) figyelni kell. Akkumulátor nélkül, csak USB-ről a töltő LED-je kiszámíthatatlanul villoghat; ez normális, az akkumulátor nélküli üzemet tartósan nem javasoljuk.
4. **Firmware-feltöltés.** Az USB-n keresztül (natív USB-Serial/JTAG), szükség esetén a BOOT1 gomb nyomva tartásával és az EN1 megnyomásával bootloader módba lépve.
5. **Tápátkapcsolás ellenőrzése.** Csak USB-ről a V_SYS ≈ 4,1 V; csak akkumulátorról a V_SYS ≈ V_BAT, a +3,3 V stabil; mindkettővel a rendszert az USB táplálja, az akkumulátort a TP4056 tölti, és az akkumulátor nem ad áramot a rendszernek. USB kihúzásakor a V_SYS és a +3,3 V hullámformáját oszcilloszkóppal ellenőrizni kell (3. ábra).

**USB-C használata:**

- A megrendelt 0,5 A-es F1 PPTC-t a töltés és az üzem együttes ≈ 0,75 A-es áramfelvétele meghaladja, ezért tartós teljes terhelésnél kioldhat (újraindulás, majd lehűlés után visszaáll); a 3.2.4 szerinti 1 A-es cseréig töltés közben ne használj nagy fényerőt. A töltés és az üzem együttes áramfelvétele ≈ 0,75 A. A CC1/CC2 lábak 5,1 kΩ-os lehúzása miatt a táplálóforrás a saját Rp értékének megfelelő áramot hirdeti meg (USB 2.0 alapértelmezés 500/900 mA, 1,5 A vagy 3 A). Az 1 A-es forrás 25 %-os tartalékot ad a 0,75 A-es csúcsra, de **500 mA-es USB 2.0 portról** a töltés és az üzem együtt túlterhelheti a forrást (feszültségesés, újraindulás). Ilyen forrásnál a töltőáram R18 növelésével csökkenthető (pl. 4,8 kΩ → 250 mA). Jó minőségű, rövid, vastag erű kábel használata javasolt.
- A modul az USB-C csatlakozón ESD-védelemmel (USBLC6-2P6) rendelkezik, de a csatlakozót nem szabad feszültség alatt, erőltetve dugni.
- **Nagy fényerő és melegedés.** Maximális háttérvilágításnál és USB-ről táplálva az LDO disszipációja ≈ 0,2 W, ami a 3.3 szerint a megengedett környezeti hőmérsékletet ≈ 85 °C-ra korlátozza. A házban (különösen a szellőzőrácsos podban) megfelelő szellőzést kell biztosítani, és melegben a fényerőt mérsékelni kell.
- Az akkumulátor mélykisütése után (védelem lekapcsolt) a cella csak USB-ről töltés indításával éleszthető újra; ha a cella nem éled fel, ne próbáljuk külső forrással „erőltetni".

---

## Függelék: Átdolgozási előzmények

**Hardver-revíziók**

| HW rev. | Dátum | Leírás |
|---|---|---|
| A (Y9) | 2026-10-11 | Első gyártásba adott NYÁK. Hiba: a Q1 2. és 3. padjának hálózata fel van cserélve (3.2.3) |
| A.1 (Y10) | 2026-10-15 | Javított NYÁK: csak a Q1 2./3. padjának hálózata és a hozzá tartozó rézvezetés változott; alkatrészlista és elrendezés azonos. Ez a NYÁK kerül az összeszerelési rendelésbe (SMT026101161535) |
| A.1-m | a kártyák megérkezése után | Tervezett kézi módosítások: F1 → 1 A, R17 ∥ 1 kΩ, C6 ∥ 1 µF, D2 → SMAJ6.0A (opcionális), lásd 3.2.4 |

**Dokumentum-revíziók**

| Rev. | Dátum | Leírás |
|---|---|---|
| A | 2026-10-12 | Első kiadás: kijelző csomópont hardverarchitektúrája, tápellátás, NYÁK és felhasználói útmutató |
| A.1 | 2026-10-13 | Kapcsolási rajz és NYÁK alapján pontosítva: C7/C8 10 µF, a Q1 source/drain bekötési hibájának dokumentálása, R_DS(on) adatlapi értékei |
| A.2 | 2026-10-15 | A gyártásba adott (javított) NYÁK szerint: J2 kiosztás (GPIO-hozzárendeléssel), a bemeneti lánc (F1, D2, D5), USB-üzemi V_SYS és LDO-veszteség újraszámolva, a gate kisülési idő újraszámolva (C10 miatt ≈ 0,5 s), 3.2.3 a javítással, új 3.2.4 (utólagos kézi módosítások), DRC-maradékok és edge rail (4.4), I²C felhúzók hiánya |
| A.3 | 2026-10-15 | Kijelző véglegesítve: Waveshare 1,85″ érintős LCD modul (SKU 35079), ST77916 + CST816S (korábban CST816D feltételezés); a J2 1–15. láb egyezése a gyártói kiosztással ellenőrizve; a 16–18. láb, az I²C-cím és a felhúzók a modul kapcsolási rajzán még ellenőrizendők |

*A dokumentum a megadott tervezési háttéranyagból (kapcsolási és NYÁK-paraméterek), a NYÁK-fájlok közvetlen vizsgálatából és mérnöki számításokból készült. A [B] jelölésű értékek becslések, amelyeket a prototípuson mérni kell; a 3.2.1 és 3.3 pontban jelzett tervezési megjegyzések a mérések alapján lezárandók.*
