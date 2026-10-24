# 🔧 03. Hardverarchitektúra

Az OpenGauge két, egymástól fizikailag független NYÁK-ból áll, amelyeket vezeték nélküli kapcsolat köt össze:

- **OBD-egység** (ESP32-C3): a J1962 csatlakozóba dugva a jármű CAN-buszát olvassa, és a járműből kapja a tápfeszültséget.
- **Kijelzőegység** (ESP32-S3): kerek érintőkijelzős, USB-ről vagy akkumulátorról táplált egység a műszerfalon.

Ez a dokumentum gyors hardverreferencia a fejlesztőknek. A levezetések, a méretezések és a teljes alkatrészjegyzék a két részletes műszaki leírásban találhatók (lásd a 6. szakaszt). Ellentmondás esetén a KiCad-projekt (kapcsolási rajz és NYÁK) az irányadó.

---

## 🗺️ 1. Rendszerfelépítés

```
                 JÁRMŰ (12 V-os hálózat, nagy sebességű CAN)
        ┌─────────────────────────────────────────────────────┐
        │  J1962 (OBD-II)                                     │
        │  16: VBAT   4/5: GND   6: CANH   14: CANL           │
        └──────────────────────────┬──────────────────────────┘
                                   ▼
                ┌──────────────────────────────────┐
                │          OBD-EGYSÉG              │
                │  ESP32-C3-WROOM-02               │
                │  CAN-illesztő + TWAI             │
                │  Buck: 12 V → 3,3 V              │
                │  Védelem: SS34, SMAJ24A          │
                └────────────────┬─────────────────┘
                                 │ ESP-NOW (2,4 GHz, fix csatorna)
                                 ▼
                ┌──────────────────────────────────┐
                │        KIJELZŐEGYSÉG             │
                │  ESP32-S3-WROOM-1-N8R8           │
                │  1,85" kerek kijelző + érintés   │
                │  USB-C + 1S LiPo, 3,3 V-os LDO   │
                └──────────────────────────────────┘
```

**Miért két panel?** A járművel érintkező, zajos villamos környezet (tranziensek, fordított polaritás, CAN-busz) az OBD-egységen marad, a kijelzőegység pedig csak a rádiós csomagokat látja, nincs galvanikus kapcsolata a jármű hálózatával. A két egység külön programozható, cserélhető és tesztelhető, egyikük hibája nem terjed át a másikra, és a kijelző a csatlakozótól független helyre szerelhető, kábelezés nélkül.

---

## 🔌 2. OBD-egység

### 2.1 Blokkvázlat

```
 J1962 16 (VBAT) ──► D8 (SS34) ──┬──► VIN ──► TPS54302 ──► +3,3 V ──► ESP32-C3, CAN-illesztő
                  └─► D4 (SMAJ24A) ► GND    ▲
 USB-C (J1) ──► F1 (PPTC) ──► D2 (SS14) ────┘

 J1962 6/14 (CANH/CANL) ──► SN65HVD231 ◄──► ESP32-C3 (TWAI: IO4 TX, IO5 RX; IO10: RS)
 USB-C D+/D− ──► USBLC6-2P6 (ESD) ──► ESP32-C3 natív USB (IO18/IO19)
```

### 2.2 Fő jellemzők

| Jellemző | Érték |
|:--|:--|
| Mikrovezérlő | ESP32-C3-WROOM-02 (RISC-V, 160 MHz, TWAI, 2,4 GHz Wi-Fi/BLE) |
| Feszültségszabályozó | TPS54302, szinkron buck; kimenet 3,296 V (R-osztó 45,3 kΩ / 10 kΩ); induktor 10 µH |
| Bemeneti tartomány | 6,5 V … 18 V folyamatos; legfeljebb 24 V rövid ideig (jump start) |
| Bemeneti védelem | D8 (SS34): fordított polaritás és diódás VAGY; D4 (SMAJ24A): TVS |
| Tápforrások | J1962 16. láb (VBAT), vagy USB-C (programozáshoz, F1 PPTC és D2 diódán át) |
| CAN-illesztő | SN65HVD231 (3,3 V), RS-láb (üzemmód) az IO10-en |
| Programozás | Natív USB-Serial/JTAG, külső USB–soros IC nélkül |
| NYÁK | 4 réteg, 1,6 mm (JLC04161H-7628) |
| Méret | kb. 41,8 × 59,1 mm; a J1962 csatlakozóval együtt kb. 80,1 mm |

### 2.3 Lábkiosztás (ESP32-C3)

| GPIO | Jel | Funkció |
|:--|:--|:--|
| IO4 | CAN_TX | A CAN-illesztő TXD bemenete (TWAI TX) |
| IO5 | CAN_RX | A CAN-illesztő RXD kimenete (TWAI RX; RTC-képes) |
| IO10 | OPMODE | A CAN-illesztő RS lába (LOW: nagy sebesség, HIGH: standby) |
| IO6 / IO7 / IO8 | LED | Zöld / kék / piros LED, 330 Ω-on át (az IO8 strapping láb) |
| IO9 | BOOT | BOOT gomb, 10 kΩ felhúzás |
| IO18 / IO19 | USB D− / D+ | Natív USB-Serial/JTAG |
| IO20 / IO21 | RXD0 / TXD0 | UART tesztpadok (J3, J4) |
| IO0–IO3 | – | Nincs bekötve |

### 2.4 Busz-lezárás és a JP1

A jármű CAN-hálózatát a busz két végén 120–120 Ω zárja le (eredő 60 Ω). Az OBD-egység a busz végpontján kívül csatlakozik, ezért járműben **nem** szabad hozzáadni további lezárást (három lezárásnál az eredő 40 Ω lenne).

> ⚠️ **A JP1-et nem szabad összeforrasztani.** A JP1 forrasztóhíd a CANH és a CANL vezetéket ellenállás nélkül köti össze, összeforrasztva tehát zárlatot okozna a buszon. A kártyán nincs lezáró ellenállás; asztali teszthez a J1962 csatlakozó 6. és 14. lába közé külső 120 Ω-os ellenállást kell kötni, ha a szimulátor nem tartalmaz lezárást.

---

## 🖥️ 3. Kijelzőegység

### 3.1 Blokkvázlat

```
 USB-C (J1) ──► F1 (PPTC) ──► D5 (SS14) ──► +5V ──┬──► TP4056 (U4) ──► LiPo (J5)
   │  ESD: USBLC6-2P6 (U3), VBUS TVS: D2          │
   │                                              └──► D8 (SS14) ──┐
   │                                                                ├──► V_SYS ──► TLV75733 (U5) ──► +3,3 V
   └──► ESP32-S3 natív USB (IO19/IO20)        LiPo (BTR+) ──► Q1 (AO3401A) ┘

 ESP32-S3-WROOM-1-N8R8 ──► J2 (18 pólusú FPC) ──► Waveshare 1,85" modul (ST77916 QSPI, CST816S I²C)
```

### 3.2 Fő jellemzők

| Jellemző | Érték |
|:--|:--|
| Mikrovezérlő | ESP32-S3-WROOM-1-N8R8 (kétmagos, 240 MHz, 8 MB Octal PSRAM, 8 MB Flash) |
| Kijelző | Waveshare 1,85″ kerek IPS modul (SKU 35079), 360 × 360, ST77916, QSPI |
| Érintés | CST816S, I²C, megszakításkimenettel (TP_INT) |
| Képkocka | 360 × 360 × 2 B ≈ 253 KiB (RGB565), ezért PSRAM szükséges; teljes képkocka átvitele 40 MHz-es QSPI-n ≈ 13 ms |
| Akkumulátor | 1S LiPo, 1000 mAh (LP803040, belső védelemmel), Molex MX-51021-0200 csatlakozó |
| Töltő | TP4056, 500 mA (R18 = 2,4 kΩ) |
| Tápútválasztás | Q1 (AO3401A P-FET) és D8: USB jelenlétében USB-ről, egyébként akkumulátorról üzemel |
| Szabályozó | TLV75733 (U5), 3,3 V, 1 A |
| Rögzítés | Három M2-es furat; a hátoldalon sík, alkatrészmentes zóna a LiPo cellának |
| NYÁK | Kör alakú, 4 réteg, 1,6 mm (JLC04161H-7628) |
| Méret | 50 mm átmérő; az USB-C-fül miatt 50 × 56,4 mm |

**A tápútválasztás működése röviden.** USB jelenlétében a V_SYS kb. 4,1 V (VBUS − F1 − D5 − D8), a Q1 gate-je magasan van, a FET zár, a TP4056 pedig tölti az akkumulátort. USB hiányában a gate az R17-en át földre húzódik, a FET bekapcsol, és az akkumulátor közvetlenül táplál. A gate kisülési ideje (R17·C10) miatt a FET bekapcsolása késik, ezalatt a Q1 testdiódája viszi a terhelést; az átváltás részletes elemzése a kijelzőegység műszaki leírásában található.

### 3.3 Lábkiosztás (ESP32-S3)

Az FPC-csatlakozó (J2, Hirose FH12-18S-0.5SH) 1–15. lába a modul gyártói kiosztását követi, a 16–18. láb földelés.

| J2 láb | Jel | GPIO | Funkció |
|:--:|:--|:--|:--|
| 1 | +3,3 V | – | Tápfeszültség |
| 2 | LCD_BL | IO42 | Háttérvilágítás (PWM) |
| 3 | GND | – | Föld |
| 4 | LCD_SCLK | IO41 | QSPI órajel |
| 5–7 | LCD_IO0 … IO2 | IO40, IO39, IO38 | QSPI D0 … D2 |
| 8 | LCD_RST | IO45 | Kijelző reset (strapping láb) |
| 9 | LCD_CS | IO48 | Chip select |
| 10 | LCD_TE | IO47 | Tearing Effect (függőleges szinkron) |
| 11 | LCD_IO3 | IO21 | QSPI D3 |
| 12 | TP_RST | IO14 | Érintő reset |
| 13 | TP_SCL | IO13 | Érintő I²C órajel |
| 14 | TP_SDA | IO12 | Érintő I²C adat |
| 15 | TP_INT | IO11 | Érintési megszakítás (RTC-képes, ébresztésre alkalmas) |

Egyéb: IO0 BOOT gomb; IO43/IO44 UART0 tesztpadok; IO19/IO20 USB D−/D+; IO15/IO16/IO17 piros/zöld/kék LED; IO7 akkumulátorfeszültség-mérés (100 kΩ / 100 kΩ osztón át). A GPIO35–GPIO37 az Octal PSRAM-é, ezek a lábak nem használhatók.

---

## 📐 4. NYÁK-kialakítás

Mindkét lap négyrétegű (JLC04161H-7628, 1,6 mm). A kijelzőegység rétegei: L1 alkatrészek és jelek, L2 megszakítatlan földsík, L3 tápelosztás (+5 V, V_BAT, +3,3 V), L4 gombok és az akkumulátor-zóna. Az OBD-egységen az L4-en fut a CAN differenciális pár is.

A legfontosabb tervezési szabályok (elméleti háttér: a szakdolgozat 1.6. pontja):

- **Buck-hurok:** a bemeneti kondenzátor, az IC és az induktor hurkát a lehető legkisebbre kell venni; a kapcsolási csomópont (SW) rövid és kis területű legyen.
- **Visszatérő áram:** a jelek alatt megszakítatlan földsík fusson; a CAN-pár és a nagy sebességű jelek (QSPI) ne keresztezzenek földsík-vágást.
- **Antenna:** a modul antennaoldala a lap szélén legyen, alatta és körülötte kizárási zóna (réz, kitöltés és alkatrész nélkül), a nagy fémfelületektől és csatlakozóktól távol.
- **Lábnyomok:** minden aktív alkatrész lábkiosztását az adatlappal és a NYÁK-padekkel is össze kell vetni, nem csak a kapcsolási rajzzal.

---

## ⚠️ 5. Ismert eltérések és javítások

A felismert hibák a fejlesztési naplóban részletesen szerepelnek (`docs/fejlesztesi_naplo.xlsx`). A főbbek:

| Egység | Eltérés | Következmény és kezelés |
|:--|:--|:--|
| OBD | A kártyán SN65HVD231 van az eredetileg tervezett SN65HVD230 helyett | A két típus lábkompatibilis, a csere a tokozás miatt (SOIC-8) kézi forrasztással lehetséges |
| OBD | A JP1 mellé nem került lezáró ellenállás | A JP1 nem forrasztható össze (zárlat); lezáráshoz külső ellenállás kell |
| OBD | A D4 (SMAJ24A) kapocsfeszültsége (38,9 V @ 10,3 A) meghaladja a TPS54302 30 V-os abszolút maximumát | Normál 12 V-os hálózatban elfogadható; elnyomatlan load dump ellen nem véd; mérendő |
| Kijelző | A Q1 source és drain lába fel volt cserélve a Rev. A tervben | A gyártás előtt felismerve javítva, új NYÁK-rendelés |
| Kijelző | Utólagos kézi módosítások: F1 0,5 A → 1 A, R17 ∥ 1 kΩ, C6 ∥ 1 µF, D2 opcionálisan SMAJ6.0A | Az alkatrészek a megrendelés után nem voltak módosíthatók, ezért kézzel cserélendők |

---

## 📎 6. Részletes dokumentáció

- `obd_telemetria_muszaki_leiras`: az OBD-egység kapcsolása, méretezései, védelme, NYÁK-ja és mérési útmutatója
- `display_node_muszaki_leiras`: a kijelzőegység kapcsolása, tápútja, kijelző-interfésze és NYÁK-ja
- `fejlesztesi_naplo.xlsx`: hibák, változtatások és tervezési döntések
