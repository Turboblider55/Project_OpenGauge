# 🚗 Projektleírás és hatókör: OpenGauge

**Projekt neve:** OpenGauge (moduláris járműtelemetria és kijelző OBD-II-n keresztül)  
**Projekt típusa:** BSc-szakdolgozati projekt  
**Dokumentum utolsó frissítése:** 2026.10.20.

---

## 📌 1. Összefoglaló és háttér

A modern személygépjárművek a belső CAN-hálózataikon sok telemetriaadatot állítanak elő, amelyek a kötelező OBD-II (J1962) diagnosztikai csatlakozón át elérhetők. A gyári műszerfalak ugyanakkor gyakran nem mutatják a fontos valós idejű jellemzőket, például a pontos hűtővíz-hőmérsékletet vagy a fojtószelep állását. A piacon kapható megoldások jellemzően két csoportba tartoznak: terjedelmes, saját fejlesztésű műszerek, amelyek zavaros tápellátási és jelkábelezéssel terhelik a műszerfalat, vagy általános okostelefon-alkalmazások, amelyek lassú kézi Bluetooth-párosítást igényelnek, és terhelik a telefon akkumulátorát.

Az **OpenGauge** moduláris, kétegységes telemetriai rendszer, amely az adatgyűjtést és a megjelenítést szétválasztja:

- 🔌 Az **OBD-egység** közvetlenül a jármű OBD-II csatlakozójába dugható, a járműből kapja a tápfeszültséget, és a CAN-buszról olvassa a diagnosztikai adatokat.
- 🖥️ A műszerfalra szerelhető **kijelzőegység** vezeték nélkül kapja az előfeldolgozott adatokat, és a vezető számára kialakított grafikus műszereket jelenít meg valós időben.

Az adatgyűjtő panel és a kijelző közötti, kis késleltetésű, kapcsolatmentes ESP-NOW rádiós összeköttetés kábelek nélküli másodlagos kijelzőt ad, amely a tervek szerint a gyújtás bekapcsolásával együtt indul.

---

## 🎯 2. Célok és sikerkritériumok

### 2.1 Mérnöki fő célok

- **[G-1]** 🛠️ Két egyedi, négyrétegű nyomtatott áramköri lap tervezése és legyártatása: az OBD-egységé (buck-áramkör, CAN-illesztő, védelmek) és a kijelzőegységé (USB-C és LiPo tápellátás, töltő, tápútválasztás, 3,3 V-os LDO).
- **[G-2]** ⚡ Beágyazott szoftver, amely az ISO 15765-4 szerinti OBD-II paraméterazonosítókat (PID) kis buszterheléssel kérdezi le és értelmezi, a CAN-sebesség (250/500 kbit/s) és az azonosítóhossz (11/29 bit) automatikus felismerésével.
- **[G-3]** 📡 Megbízható, kis késleltetésű, kapcsolatmentes, egyenrangú (peer-to-peer) vezeték nélküli adatkapcsolat az OBD-egység és a kijelzőegység között.
- **[G-4]** 📊 Reszponzív grafikus felület a kerek 1,85″-es, 360 × 360 pixeles kijelzőn, amely legalább 30 FPS-sel jeleníti meg a műszerek animációit, a kommunikációs feladatok blokkolása nélkül.

### 2.2 Sikermutatók

A felsorolt értékek **célértékek**, a mérésük a szakdolgozat 5. fejezetében történik.

- 🔋 **Tápellátás stabilitása:** az OBD-egység buck-áramköre 9,0 V és 16,0 V közötti bemeneti feszültségen megbízhatóan működik, túlmelegedés vagy lekapcsolás nélkül.
- 🛡️ **Buszintegritás:** a lekérdezési ciklus nem okoz hibakeretet a jármű nagy sebességű CAN-hálózatán.
- ⏱️ **Frissítési sebesség:** a fordulatszám legalább 10 Hz-cel, a sebesség legalább 5 Hz-cel frissül a kijelzőn (a PID-terv szerint), a késleltetés legfeljebb 100 ms.
- 🚀 **Indulási idő:** a gyújtás bekapcsolásától az első érvényes adat megjelenéséig legfeljebb **3 másodperc**. Az akkumulátoros alvó üzemmódból való ébredés ideje külön mérendő.

---

## 🔍 3. Hatókör és határok

### 3.1 A hatókörben lévő (vállalt) eredmények

- 📐 **Egyedi hardver:** mindkét egység kapcsolási rajzának és NYÁK-jának tervezése, alkatrészválasztás, gyártatás, beültetés (JLCPCB), üzembe helyezés és mérés.
- 🔌 **Tápáramkörök:** az OBD-egységen lépcsős (buck) feszültségszabályozás fordított polaritás elleni védelemmel, bemeneti szűréssel és tranziensvédelemmel (TVS); a kijelzőegységen töltő, tápútválasztás és LDO.
- 💻 **Szoftver, OBD-egység:** a CAN/TWAI periféria inicializálása, az OBD-II PID-kérések ciklikus ütemezése (Mode 01), ISO-TP kezelés, az üzenetek értelmezése és az ESP-NOW-adás.
- 🎨 **Szoftver, kijelzőegység:** az ESP-NOW-csomagok fogadása, érvényesítés, és az LVGL grafikus könyvtárral készült felület QSPI-n meghajtott kijelzőn.
- 🖨️ **Mechanikai terv:** 3D nyomtatható házak mindkét egységhez (OBD-csatlakozó-burkolat, műszerfal- vagy szellőzőrács-tartó).

### 3.2 A hatókörön kívül (kifejezetten kizárt)

- ❌ **Hibakódok (DTC) törlése és bármilyen író szolgáltatás:** az eszköz kizárólag olvasó marad. A hibakódok törlése a készenléti monitorokat is nullázná, és ha az eszköz hibát tudna törölni, a jármű működésébe is beavatkozhatna. A hibakódok *olvasása* a tervek szerint megvalósítható.
- ❌ **Gyártóspecifikus CAN-visszafejtés:** a nem szabványos, gyártói PID-ek (például saját BMW-, VAG- vagy Ford-buszcsomagok) kizártak; a hatókör a törvényben előírt OBD-II PID-ekre korlátozódik.
- ❌ **Teljes felbontású videó vagy képernyőtükrözés:** nincs Android Auto vagy Apple CarPlay képtükrözés. A kijelző vektoros felület megjelenítésére készült, nem videódekódolásra.
- ❌ **Beavatkozás a jármű működésébe:** a rendszer soha nem küld beavatkozó (gáz, kormány, fék vagy karosszéria-vezérlő) kereteket.
- ❌ **Tanúsítás:** az EMC-, jóváhagyási és típusbizonyítási eljárások nincsenek a projekt hatókörében.

---

## 🚦 4. Funkciók szintezése és az MVP korlátai

A szintek a fejlesztési naplóban a P1–P3 prioritásoknak felelnek meg.

```
┌───────────────────────────────────────────────────────────┐
│            P1: minimálisan életképes termék (MVP)         │
│  • Két egyedi NYÁK (OBD-egység, kijelzőegység)            │
│  • CAN-lekérdezés: fordulatszám, sebesség, hűtővíz        │
│  • Egyenrangú vezeték nélküli telemetria (ESP-NOW)        │
│  • Alap digitális műszer és számkijelzés a képernyőn      │
└─────────────────────────────┬─────────────────────────────┘
                              │ (ha az ütemterv engedi)
                              ▼
┌───────────────────────────────────────────────────────────┐
│                 P2: mérnöki kidolgozás                    │
│  • Alacsony fogyasztású alvó üzemmód motor leállításakor  │
│  • Érintés, másodlagos nézet, akkumulátor-töltöttség      │
│  • VIN-lekérdezés, ECU-szimulátor a teszteléshez          │
│  • Házak 3D nyomtatása                                    │
└─────────────────────────────┬─────────────────────────────┘
                              │ (szigorúan opcionális)
                              ▼
┌───────────────────────────────────────────────────────────┐
│                 P3: kiterjesztett célok                   │
│  • BLE médiavezérlés: iOS (Apple Media Service),          │
│    Android (HID); műsorszám és előadó kijelzése iOS-en    │
│  • Hibakódok olvasása (Mode 03), csak olvasás             │
│  • Márkafelismerés a VIN-ből, indulási animáció           │
└───────────────────────────────────────────────────────────┘
```

### Az MVP kritikus műszaki korlátai

- ⚡ **A busz terhelhetőségének védelme:** az OBD-egység a busz végpontján kívül csatlakozik, ezért járműben a CAN-buszon nem lehet további 120 Ω-os lezárás. A gyártott kártyán **nincs** lezáró ellenállás: a JP1 forrasztóhíd az ellenállás nélkül közvetlenül a CANH és a CANL között van, ezért **nem szabad összeforrasztani** (zárlat). A JP1 mellé nem került ellenállás-lábnyom, ez a hibanaplóban dokumentált tervezési hiba. Asztali teszthez külső lezárást kell használni.
- 📏 **Panelméretek:** az OBD-egység panelje kb. 41,8 mm × 59,1 mm, a J1962 csatlakozóval együtt kb. 80,1 mm hosszú. A beépítés nem akadályozhatja a lábtér kezelőszerveit, ezt a járműben kell ellenőrizni. A kijelzőegység panelje kör alakú, 50 mm átmérőjű, az USB-C-fül miatt 50 mm × 56,4 mm.
- 📦 **Alkatrész-elérhetőség:** minden aktív és passzív alkatrészt a szokásos gyors szerelési szolgáltatók (JLCPCB SMT-könyvtár, LCSC) raktáron lévő termékeiből kell választani.

---

## 📦 5. Projekt-eredmények

| Azonosító | Kategória | Leírás | Formátum / tárgy |
| :--- | :--- | :--- | :--- |
| **DEL-01** | 📄 Dokumentáció | Teljes rendszerterv és funkcionális leírás (magyarul) | Markdown / Word a `/docs` mappában |
| **DEL-02** | 🖥️ Hardver | KiCad kapcsolási rajz, NYÁK-terv, Gerber- és fúrófájlok, mindkét egységhez | `.kicad_sch`, `.kicad_pcb`, zip a `/hardware` mappában |
| **DEL-03** | 📋 Hardver | Alkatrészjegyzék (BOM) LCSC-cikkszámokkal | `bom.csv` |
| **DEL-04** | 💻 Szoftver | OBD-egység szoftvere (CAN-értelmezés és vezeték nélküli adás) | C++ forráskód a `/firmware/obd_node` mappában |
| **DEL-05** | 🎨 Szoftver | Kijelzőegység szoftvere (LVGL felület és vezeték nélküli vétel) | C++ forráskód a `/firmware/display_node` mappában |
| **DEL-06** | 🖨️ Mechanika | Házak CAD-modelljei 3D nyomtatáshoz | `.step` / `.stl` a `/mechanical` mappában |
| **DEL-07** | 🎓 Tanulmányi | Szakdolgozat és bemutató videó | Szakdolgozat (Word/PDF) |

---

## ⚠️ 6. Kockázatkezelés és tartalékterv

| Azonosított kockázat | Súlyosság | Valószínűség | Kezelési stratégia |
| :--- | :--- | :--- | :--- |
| **NYÁK-tervezési vagy gyártási hiba** (például hibás lábnyom, rossz alkatrész) | 🔴 Magas | 🔴 Magas (be is következett) | Az előfordult hibák (SN65HVD231 a tervezett SN65HVD230 helyett, JP1 ellenállás nélkül, Q1 lábnyom, kézi alkatrészcserék) a hibanaplóban szerepelnek; javítás pótalkatrészekkel és kézi módosítással, a következő verzióban a tervezési ellenőrzőlista bővítése. |
| **Járműkompatibilitási hiba** (ECU időtúllépés, néma busz) | 🔴 Magas | 🟢 Alacsony | Fejlesztés asztali környezetben ECU-szimulátorral (ESP32-S2) és USB–CAN adapterrel, csak ezután valós járművön. |
| **A kijelző képfrissítési lemaradása** (nagy képkocka, QSPI-sávszélesség) | 🟡 Közepes | 🟡 Közepes | A teljes képkocka PSRAM-ban, LVGL részleges frissítéssel és DMA-puffereléssel; a magfoglalás szétválasztása (0. mag: rádió, 1. mag: LVGL). |
| **A Bluetooth-telefonoldali verem bonyolultsága** (ütemezési kockázat) | 🟡 Közepes | 🔴 Magas | A BLE-/média-funkciók szigorúan a P3 szinten; ha az ütemterv szerint le vagyunk maradva, a BLE-integráció a sikerkritériumok veszélyeztetése nélkül elhagyható. |
| **Alkatrész- vagy szállítási késedelem** (egyes tételek 10–15 munkanap) | 🟡 Közepes | 🟡 Közepes | A szoftverfejlesztés fejlesztői panelekkel és szimulátorral a hardver rendelkezésre állásától függetlenül haladhat; a hardverfüggő tesztek az alkatrészek megérkezése után végezhetők. |
