# MŰSZAKI LEÍRÁS (ENGINEERING SPECIFICATION)

## OBD-II vezeték nélküli telemetria csomópont – ESP32-C3 / TPS54302 / SN65HVD231

| Mező | Érték |
|---|---|
| Dokumentum azonosító | ES-OBD-TLM-001 |
| Verzió / állapot | Rev. A – tervezői vázlat, felülvizsgálatra |
| Dátum | 2026. október 7. |
| Termék | OBD-II interfész modul („OBD Node") ESP-NOW adatkapcsolattal |
| Kapcsolódó egység | Körműszeres műszerfali kijelző („Display Node") |
| Hatály | Hardver (kapcsolás, NYÁK, ház) és beágyazott firmware-architektúra |

**Jelölésrendszer.** A dokumentumban a *[T]* jelölés tervezési adatot (kapcsolási rajz, alkatrészlista), a *[K]* követelményt, a *[B]* becslést vagy számított várható értéket jelöl, amelyet a 6. fejezet szerinti méréssel igazolni kell. A képletek kódblokkban, tizedesponttal szerepelnek; a folyó szövegben és a táblázatokban tizedesvessző van.

**Hivatkozott szabványok és dokumentumok:** SAE J1962 (diagnosztikai csatlakozó); ISO 15765-4 (CAN-alapú diagnosztika, kibocsátáshoz kötött OBD); ISO 11898-2 (nagysebességű CAN fizikai réteg); SAE J1979 / ISO 15031-5 (OBD-II szolgáltatások és PID-ek); ISO 7637-2 (tranziens vezetett zavarok); ISO 16750-2 (elektromos terhelések, load dump); Texas Instruments TPS54302 és SN65HVD231 adatlap; Espressif ESP32-C3 és ESP32-C3-WROOM-02 adatlap, ESP-IDF TWAI és ESP-NOW programozási útmutató; JLCPCB 4 rétegű stackup (JLC04161H-7628).

---

# 1. RENDSZERÁTTEKINTÉS ÉS KÖVETELMÉNYEK

## 1.1 A projekt célja

A projekt egy járműipari minőségű OBD-II interfészmodul tervezése, amely közvetlenül a jármű SAE J1962 diagnosztikai portjába csatlakozik. A modul az ISO 15765-4 szerinti nagysebességű CAN buszon (500 kbit/s) valós idejű motortelemetriai adatokat kérdez le (fordulatszám, sebesség, hűtővíz-hőmérséklet, fojtószelep-állás), majd ezeket alacsony késleltetésű, pont-pont 2,4 GHz-es vezeték nélküli kapcsolaton, ESP-NOW protokollal továbbítja egy körméretű műszerfali kijelzőhöz.

A modul önálló egység: a tápellátását a diagnosztikai port 16-os kivezetéséről (VBAT) kapja, külön vezeték vagy akkumulátor nem szükséges. Firmware-frissítés és diagnosztikai naplózás a natív USB-Serial/JTAG interfészen keresztül lehetséges külső USB–soros illesztő IC nélkül.

## 1.2 Mérnöki indoklás: miért nem megfelelő az ELM327 és miért szükséges az ESP-NOW

A kereskedelmi forgalomban kapható Bluetooth ELM327 klónok architekturálisan nem alkalmasak folyamatos, nagy frissítési sebességű analóg műszerfal-animáció kiszolgálására. A korlátok okai:

1. **Szöveges (ASCII) parancsértelmezés.** Minden lekérdezés AT/OBD parancs szöveges hexadecimális kódolásban („010C"), amelyet az ELM327 firmware értelmez, majd a válasz ugyancsak ASCII-ban, parancs-prompt (`>`) lezárással érkezik vissza. A kódolás, az echo és a prompt-szinkronizáció saját feldolgozási időt jelent.
2. **Soros, kérés–válasz alapú működés.** Az ELM327 egyszerre egy tranzakciót kezel; a következő PID kérése csak az előző válasz megérkezése után indítható.
3. **Bluetooth SPP réteg.** A klasszikus Bluetooth soros profil (RFCOMM) csomagolási és ütemezési késleltetést visz a láncba, amely nem determinisztikus.
4. **Mért eredő késleltetés:** paraméterenként tipikusan **40–80 ms**.

A követelmény ezzel szemben folyamatos, 40–60 Hz-es megjelenítési frissítés, ami képkockánként 16,7–25 ms időkeretet jelent.

| Tulajdonság | Bluetooth ELM327 klón | Dedikált ESP-NOW csomópont |
|---|---|---|
| Adatformátum | ASCII hex szöveg | Bináris, 16 bájtos csomag |
| Késleltetés paraméterenként | 40–80 ms | < 4 ms (vezeték nélküli szakasz) |
| 4 paraméter egy ciklusban | 160–320 ms (≈ 3–6 Hz) | ECU-függő (cél: ≥ 30–40 Hz) |
| Kapcsolatfelépítés | Párosítás, SPP csatornaépítés | Előre konfigurált peer, kapcsolatmentes |
| Rádiós protokollverem | Bluetooth + RFCOMM | 802.11 vendor-specifikus akciókeret, TCP/IP nélkül |
| Ütemezés | Nem determinisztikus | Az alkalmazás vezérli (frissebb adat elsőbbsége) |

Az ESP-NOW (Espressif) kapcsolatmentes, kis overhead-ű protokoll, amely IP-verem, DHCP, kapcsolatfelvételi kézfogás nélkül, közvetlenül 802.11 vendor-specifikus keretekben továbbítja a legfeljebb 250 bájtos adatcsomagokat. A 16 bájtos telemetriacsomag légi ideje 1 Mbit/s-os alapsebességnél is nagyságrendileg 1 ms (fejléc és előtag miatt kb. 60 bájtnyi keret), így a vezeték nélküli szakasz késleltetése a 4 ms-os határ alatt marad, és nem korlátozza a mintavételi frekvenciát. A tényleges szűk keresztmetszet ezért az ECU válaszideje, amelyet a 4.2 szerinti lekérdezési ütemezés kezel.

## 1.3 Rendszerarchitektúra (szöveges blokkdiagram)

```
                       JÁRMŰ (500 kbit/s High-Speed CAN, ISO 15765-4)
                                         |
                      +------------------+-------------------+
                      |         SAE J1962 Type-A (OBD-II)    |
                      |  Pin16: VBAT   Pin4/5: GND   Pin6/14: CANH/CANL
                      +---------+-------------------+--------+
                                |                   |
                        [TRANZIENS VÉDELEM]         |
                       F1(PPTC) -> D1(SS14) -> D4(TVS) || C7                 
                                |                   |
                         +12V védett                |
                                |                   |
                  +-------------v----+      +-------v--------+
                  |  TPS54302 BUCK   |      | SN65HVD231     |
                  |  400 kHz, L1=10uH|      | CAN transceiver|
                  +-------------+----+      |  (3.3 V)       |
                                |           +--+-----+----+--+
                             +3.3 V            |TXD  |RXD |RS (standby)
                                |              |     |    |
                   +------------v--------------v-----v----v----+
                   |            ESP32-C3-WROOM-02              |
                   |  RISC-V 160 MHz | TWAI (CAN 2.0B)         |
                   |  USB Serial/JTAG | 2,4 GHz Wi-Fi (ESP-NOW)|
                   +--------+---------------------+------------+
                            |                     |
                        USB (D+/D-)        2,4 GHz, ESP-NOW (unicast)
                     (firmware, napló)              |
                                          +---------v----------+
                                          |   DISPLAY NODE     |
                                          | körműszeres kijelző|
                                          +--------------------+
```

## 1.4 Működési paraméterek

| Paraméter | Érték | Megjegyzés |
|---|---|---|
| Bemeneti tápfeszültség, folyamatos üzem | 6,5 V … 18 V | 12 V-os fedélzeti hálózat, töltés közben 14,4 V *[K]* |
| Bemeneti tartomány, kiterjesztett | legfeljebb 24 V, rövid ideig (jump start, ≤ 60 s) | A D4 TVS állandó záróirányú feszültsége (VRWM = 24 V) határolja *[B]* |
| Védelem | Fordított polaritás, PPTC, TVS | 2. fejezet *[T]* |
| Rendszerfeszültség | +3,3 V (3,296 V névleges) | TPS54302, R2/R3 osztó *[T]* |
| Tervezési terhelhetőség (3,3 V sín) | 0,6 A | Az induktorméretezés alapja; az IC névleges 3 A-ével szemben bő tartalék *[T]* |
| Átlagos áramfelvétel, aktív (ESP-NOW folyamatos küldés) | ≈ 30–40 mA @ 12 V | ≈ 100 mA @ 3,3 V átlag, η ≈ 85 % feltételezéssel *[B]* |
| Csúcs áramfelvétel, aktív | ≤ 0,2 A @ 12 V (≤ 0,36 A @ 6,5 V) | Az F1 500 mA tartóáramán belül *[B]* |
| Áramfelvétel alvó módban | cél: ≤ 1 mA @ 12 V | ESP32-C3 deep sleep + CAN standby (< 5 µA) + buck nyugalmi áram + visszacsatoló osztó *[B]* |
| CAN adatsebesség | 500 kbit/s, 11 bites azonosítók | ISO 15765-4 *[K]* |
| OBD válaszidő-limit | P2 ≤ 50 ms (szoftveres timeout) | ISO 15765-4 / 15031-5 *[K]* |
| Vezeték nélküli adatkapcsolat | ESP-NOW, 2,4 GHz, pont-pont | < 4 ms késleltetés *[K]* |
| Megjelenítési frissítés | 40–60 Hz | A kijelző oldali interpolációval együtt *[K]* |
| Üzemi hőmérséklet | −40 … +85 °C | Az ESP32-C3-WROOM-02 modul, az X7R passzívak (125 °C) és a TPS54302 tartományán belül; a ház anyaga a korlátozó tényező (5.4) *[K]* |
| NYÁK | 4 réteg, 1,6 mm, JLC04161H-7628 | 5. fejezet *[T]* |

> **Tervezési megjegyzés (üzemi tartomány).** A TPS54302 névleges bemeneti tartománya 4,5–28 V, ugyanakkor a D4 (SMAJ24A) állandó záróirányú feszültsége 24 V, a letörési feszültség minimuma 26,7 V. A 24 V feletti tartós üzem ezért nem garantált: a TVS ilyenkor már mérhető szivárgó áramot vezet. A modul tehát 12 V-os járműhálózatra, jump start esetén legfeljebb 24 V-ig (rövid ideig) kerül specifikálásra. A 28 V-ot a konverter határaként kezeljük (az induktorszámítás 18 V-ra, a ripple-ellenőrzés 28 V-ra is elvégezve), nem tartós üzemi pontként. 24 V-os haszonjárműhálózathoz nagyobb VRWM-ű TVS (pl. 33 V-os típus) és a konverter bemeneti határainak újraellenőrzése szükséges.

---

# 2. TÁPEGYSÉG ÉS JÁRMŰIPARI TRANZIENS VÉDELEM

## 2.1 Járműipari zavarok és tranziensek

A járműfedélzeti hálózat a laboratóriumi 12 V-tól lényegesen zajosabb környezet. A vezetett tranziens zavarokat az ISO 7637-2 impulzusokkal, a terhelés-lekapcsolást (load dump) az ISO 16750-2 is leírja. Az alábbi táblázat a 12 V-os rendszerre vonatkozó irányadó értékeket mutatja; a pontos szint és a vizsgálati osztály (Level) a szabvány aktuális kiadásától és a megrendelői előírástól függ.

| Impulzus | Jelleg | Tipikus amplitúdó (12 V) | Belső ellenállás | Időtartam |
|---|---|---|---|---|
| 1 | Induktív terhelés lekapcsolása a tápról (negatív tüske) | −75 … −150 V | 10 Ω | ≈ 2 ms |
| 2a | Párhuzamos fogyasztó hirtelen áramkimaradása (a huzalozás induktivitása miatt) | +37 … +112 V | 2 Ω | ≈ 0,05 ms |
| 2b | Egyenáramú motor generátoros utózengése gyújtáskikapcsolás után | ≈ +10 V | 0 … 0,05 Ω | 0,2 … 2 s |
| 3a / 3b | Kapcsolási tranziensek (burst) | −112 … −220 V / +75 … +150 V | 50 Ω | ≈ 0,1 µs impulzusok, ismétlődően |
| 4 | Indítómotor (cranking) feszültségbeesés | −6 … −7 V-ig csökkenő feszültségszint | – | ≈ 15–40 ms |
| 5a / 5b | Terhelés-lekapcsolás (load dump), nem / részben elnyomott | 65 … 87 V (5a); központi elnyomással ≈ 35 V (5b) | 0,5 … 4 Ω | 40 … 400 ms |

**Load dump jelenség.** Amikor az akkumulátor a generátor töltése közben (nagy terhelés mellett) lecsatlakozik, a generátor gerjesztési árama az állórész-mező lecsengéséig tovább termel energiát, így a fedélzeti hálózat feszültsége 40–400 ms alatt tipikusan 65–87 V-ra emelkedhet. Ez a legnagyobb energiájú tranziens, amelyet a fogyasztónak el kell viselnie. Korszerű járművekben a generátor általában belső (központi) feszültségkorlátozással rendelkezik, így a ténylegesen megjelenő amplitúdó jellemzően 35 V körüli (5b).

A CAN-busz oldalon a transzceiver és a vezérlő védelmét a transzceiver szabványos busz-túlfeszültség-tűrése adja (3. fejezet); a tápoldali védelem az alábbi lánc feladata.

## 2.2 A védelmi lánc felépítése és működése

A tápág sorrendje a diagnosztikai port felől a konverter felé *[T]*:

```
J1962 Pin16 (VBAT) --> F1 (PPTC) --> D1 (SS14) --+--> +12V_PROT --> C7 --> TPS54302 VIN
                                                 |
                                                 +--> D4 (SMAJ24A) --> GND
```

A testelés a 4-es (Chassis GND) és 5-ös (Signal GND) lábakon történik.

### 2.2.1 F1 – öngyógyuló polifuse (PPTC)

| Paraméter | Érték |
|---|---|
| Tartóáram (IHOLD) | 500 mA |
| Leoldási küszöb (ITRIP) | 1,0 A |

Az F1 feladata a lánc végső hibaáram-korlátozása: ha a TVS tartósan vezet (túlfeszültség), a buck zárlatos, vagy a fordított polaritású bekötés áramot kényszerít a körre, a PPTC felmelegedés hatására nagy ellenállású állapotba kerül, és a hiba megszűnése után regenerálódik. Méretezési ellenőrzés: a legnagyobb normál üzemi bemeneti áram (0,6 A kimeneti terhelés, η ≈ 85 %) a legkisebb bemeneti feszültségnél

```
I_in,max = (Vout * Iout) / (eta * Vin_min) = (3.3 * 0.6) / (0.85 * 6.5) = 0.358 A  <  I_HOLD = 0.5 A
```

Az áramfelvétel tehát a tartóáram alatt marad, 28 %-os tartalékkal. Az F1 sorosan ≈ 0,5 Ω-os ellenállást ad *[B, az adatlap szerint ellenőrizendő]*, ezért a buck bemenetén a legrosszabb esetben (VBAT = 6,5 V):

```
Vin,buck ≈ 6.5 V - Vf(D1) - I*R(F1) ≈ 6.5 - 0.4 - 0.36*0.5 ≈ 5.9 V   >   UVLO / 4.5 V min.
```

### 2.2.2 D1 – SS14 Schottky-dióda (fordított polaritás elleni védelem)

Az SS14 (1 A, 40 V-os Schottky) a fordított polaritású akkumulátorcsatlakozás ellen zár. Egyirányú kapuként működik: nyitó irányban kis (≈ 0,3–0,5 V) feszültségesést ad, ezért a buck számára elérhető bemeneti tartomány alig csökken, záró irányban a védett áramkör feszültsége 0 V közelében marad. A fedélzeti hálózat 24 V-os jump start szintje (ISO 16750-2) alatta marad az SS14 40 V-os záróirányú határának.

### 2.2.3 D4 – SMAJ24A egyirányú TVS-dióda

| Paraméter | Érték |
|---|---|
| Záró (standoff) feszültség VRWM | 24 V |
| Letörési feszültség VBR | 26,7 V (min.) |
| Maximális kapocsfeszültség VC | 38,9 V @ IPP = 10,3 A |
| Impulzusteljesítmény | 400 W (10/1000 µs) |

A TVS-dióda egy határolt feszültségű söntként működik: a névleges üzemi feszültségen záró irányban elhanyagolható szivárgású, a letörési feszültség fölött avalanche-üzemben vezet, és a tüske energiáját elnyeli. Az impulzusteljesítmény ellenőrzése:

```
P_pp = V_C * I_PP = 38.9 V * 10.3 A = 400.7 W   (a megadott 400 W-os kategória)
```

A C7 (100 nF / 50 V, X7R) a gyors, nagyfrekvenciás komponensek szűrését végzi a TVS reakcióideje alatt.

### 2.2.4 Méretezési értékelés és nyitott kérdések

| Tétel | Értékelés |
|---|---|
| Üzemi feszültség ↔ VRWM | 12 V-os rendszer (≤ 18 V) mellett nagy tartalék; jump start (24 V) a VRWM határán *[megfelel]* |
| Kapocsfeszültség ↔ konverter bemeneti határ | VC = 38,9 V csak 10,3 A-nél jelentkezik; az F1/D1 soros impedanciája és az áramkorlátozás miatt a buck bemenetén a tényleges csúcsérték alacsonyabb. A TPS54302 abszolút maximális bemeneti feszültségét (≈ 30 V, adatlap szerint) a **mért kapocsfeszültséggel** ellenőrizni kell *[nyitott: mérés]* |
| IPP ↔ rövid impulzusok | A 10,3 A a 10/1000 µs-os hullámalakra vonatkozik; a 2a impulzus (≈ 50 µs) esetén a TVS impulzusáram-tűrése lényegesen nagyobb, így a névleges érték itt nem korlátozó *[megfelel]* |
| Load dump (5a, elnyomatlan, 65–87 V, 0,5–4 Ω) | A TVS energiatűrése ezt önmagában nem fedi; részben elnyomott (5b, ≈ 35 V) hálózatra készült. Elnyomatlan terheléshez nagyobb teljesítményű TVS (SMDJ család) javasolt *[korlát]* |
| Negatív impulzusok (1, 3a) ↔ SS14 (40 V) | A −75 … −150 V-os, 10 Ω-os impulzus meghaladhatja az SS14 záróirányú határát; a TVS elhelyezése (D1 előtt vagy után) és a negatív oldal védelme (pl. kétirányú TVS) a tényleges vizsgálati szinthez igazítandó *[nyitott: vizsgálat]* |

---

## 2.3 TPS54302 szinkron buck konverter

### 2.3.1 Működési elv

A TPS54302 szinkron lefelé léptető (buck) konverter, 400 kHz-es, belső oszcillátorral rögzített kapcsolási frekvenciával (T = 2,5 µs). A kapcsolóelem a bemeneti feszültséget kitöltési tényezővel (D) szeleteli, az L1 induktor és a kimeneti kondenzátorok pedig egyenfeszültséggé simítják:

```
D = Vout / Vin          (ideális, veszteségek nélkül)
t_on = D * T = D / f_sw

Vin = 6.5 V  -> D = 0.508, t_on = 1.27 us
Vin = 14.4 V -> D = 0.229, t_on = 0.573 us
Vin = 18 V   -> D = 0.183, t_on = 0.458 us
Vin = 28 V   -> D = 0.118, t_on = 0.295 us
```

Könnyű terhelésnél az IC adatlap szerinti energiatakarékos (Eco-mode) működése átmenetileg csökkentheti a kapcsolási frekvenciát; a lenti számítások a rögzített 400 kHz-es, közepes–nagy terhelésű üzemre vonatkoznak.

### 2.3.2 Az L1 induktor méretezése (levezetés)

Az induktor árama a bekapcsolt állapotban lineárisan nő, a kikapcsolt állapotban lineárisan csökken. A kikapcsolt szakaszban a kimenet felé (Vout) a feszültség az induktoron −Vout, ezért a ripple-áram a kikapcsolt idő alatt:

```
Delta_IL = Vout * (1 - D) / (L * f_sw)

A (1 - D) = (Vin - Vout) / Vin helyettesítéssel, L-re rendezve:

L = [ Vout * (Vin_max - Vout) ] / [ Vin_max * f_sw * Delta_IL ]
```

A ripple-áramot a terhelési áramhoz viszonyított K_IND tényezővel adjuk meg (Delta_IL = K_IND · Iout,max). A tervezési bemenetek *[T]*:

| Bemenet | Érték |
|---|---|
| Vin,max (méretezési pont) | 18 V |
| Vout | 3,3 V |
| f_sw | 400 kHz |
| K_IND | 1,144 |
| Iout,max (tervezési terhelés) | 0,6 A |
| Delta_IL | 0,686 A (= 1,144 · 0,6 A) |

```
L = [ 3.3 * (18 - 3.3) ] / [ 18 * 400e3 * 0.686 ]
  = 48.51 / 4.939e6
  = 9.82 uH   ->  szabványos érték: 10 uH
```

A legnagyobb ripple a legnagyobb bemeneti feszültségnél jelentkezik, ezért a méretezést Vin,max pontra végezzük. A kiválasztott alkatrész: **CENKER CKST0503-10uH/M** (JLCPCB: C3002622), árnyékolt SMD kivitel (5,7 × 5,1 mm), ISAT = 3,5 A, IDC = 2,5 A, DCR = 126 mΩ.

**Az induktorra gyakorolt hatások ellenőrzése 10 µH-val (Iout = 0,6 A):**

| Vin | D | Delta_IL (10 µH) | K_IND | I_peak = Iout + Delta_IL/2 |
|---|---|---|---|---|
| 6,5 V | 0,508 | 0,406 A | 0,68 | 0,803 A |
| 14,4 V | 0,229 | 0,636 A | 1,06 | 0,918 A |
| 18 V | 0,183 | 0,674 A | 1,12 | 0,937 A |
| 28 V | 0,118 | 0,728 A | 1,21 | 0,964 A |

```
Delta_IL(Vin) = Vout * (Vin - Vout) / (Vin * L * f_sw)    pl. 18 V-on: 48.51 / (18 * 10e-6 * 400e3) = 0.674 A

I_L,rms = sqrt( Iout^2 + Delta_IL^2 / 12 ) = sqrt( 0.36 + 0.0379 ) = 0.631 A     (Vin = 18 V)
P_DCR   = I_L,rms^2 * DCR = 0.398 * 0.126 = 0.050 W
```

Az ellenőrzés eredménye: az induktor csúcsárama (< 1 A) a 3,5 A-es telítési áram (ISAT) kevesebb mint 30 %-a, az effektív áram (0,63 A) a 2,5 A-es IDC negyede, a DCR-veszteség 50 mW. Az induktor termikusan és mágnesesen is bő tartalékkal dolgozik.

### 2.3.3 Miért 10 µH és nem 33 µH? (dinamikus terhelési ugrás)

Az ESP32-C3 áramfelvétele a rádió bekapcsolásakor (adás indítása) néhány mikroszekundum alatt több száz mA-t ugrik (nyugalmi ≈ 20–30 mA-ről 300 mA feletti csúcsra). Ekkor az induktoráram a konverter szabályozásának megfelelően csak az induktor meredekségével (dI/dt) követheti a terhelést; a hiányzó töltést a kimeneti kondenzátorok szolgáltatják, ami a 3,3 V-os sín beesését okozza.

```
dI/dt (áramnövelés) = (Vin - Vout) / L

Vin = 14.4 V:  10 uH -> 1.11 A/us      33 uH -> 0.336 A/us
Delta_I = 0.3 A felzárkózási idő:      10 uH -> 0.27 us   33 uH -> 0.89 us

Elvi beesés (ideálisan gyors szabályozóhurok mellett, az induktor miatti alsó korlát):
Delta_V ≈ L * Delta_I^2 / ( 2 * C_eff * (Vin - Vout) )
```

Számítás Delta_I = 0,3 A és C_eff ≈ 22 µF (a 2 × 22 µF nominális kapacitás, az X7R dielektrikum DC-előfeszítés miatti kb. 50 %-os csökkenésével) mellett:

| Vin | 10 µH: elvi beesés | 33 µH: elvi beesés | Arány |
|---|---|---|---|
| 6,5 V | 6,4 mV | 21,1 mV | 3,3× |
| 14,4 V | 1,8 mV | 6,1 mV | 3,3× |
| 18 V | 1,4 mV | 4,6 mV | 3,3× |

Az abszolút értékek elvi alsó korlátok (a szabályozóhurok véges sávszélessége miatt a ténylegesen mért beesés ennél nagyobb), de arányuk az induktivitással egyenesen nő. A legrosszabb eset kis bemeneti feszültségnél (6,5 V, pl. hidegindításkor), mert ott a (Vin – Vout) különbség kicsi, vagyis az induktor árama lassan nő. Itt a 10 µH-s választás 3,3-szor kisebb áramtorzítást ad, mint a 33 µH.

A választás kompromisszumát is vállalni kell:

| Szempont | 10 µH | 33 µH |
|---|---|---|
| Ripple-áram (Vin = 18 V) | 0,674 A (K = 1,12) | 0,204 A (K = 0,34) |
| Csúcsáram (Vin = 18 V) | 0,937 A | 0,702 A |
| Terhelési ugrás utáni követés | gyorsabb (3,3×) | lassabb |
| Kimeneti ripple, becsült | ≈ 8–10 mVpp | ≈ 3 mVpp |
| Kisugárzott/vezetett EMI | nagyobb ripple-áram | kisebb ripple-áram |
| Méret, DCR | kisebb, kisebb DCR | nagyobb, nagyobb DCR |

A 10 µH-s választás mellett szól, hogy a rádiós terhelési ugrás a rendszer legkritikusabb dinamikus eseménye, a kapott ripple (≈ 8–10 mVpp) a 15 mVpp-s követelmény alatt marad, az induktor pedig a nagyobb ripple-áram mellett is tartalékkal dolgozik. Az EMI szempontjából fennmaradó hátrány a 2. és 5. fejezet szerinti szűréssel és elrendezéssel kezelhető.

### 2.3.4 Az induktor saját rezonancia-frekvenciája (SRF)

**Elmélet.** Minden valódi induktor a menetek közötti (és a kivezetések, a mag felé mutató) szórt kapacitással rendelkezik. Ez egy párhuzamos Cp kapacitásként modellezhető; az induktivitás és a Cp párhuzamos LC-rezonanciát alkot:

```
f_SRF = 1 / ( 2 * pi * sqrt( L * Cp ) )
```

Az SRF alatt az alkatrész induktív (|Z| ≈ 2πfL, a frekvenciával nő). Az SRF-nél az impedancia maximális, fölötte az alkatrész kapacitív viselkedésű, vagyis az impedanciája a frekvenciával csökken, és a nagyfrekvenciás zavarokat nem fojtja, hanem átengedi.

**Gyakorlati becslés.** A 10 µH-s árnyékolt SMD induktoroknál a szórt kapacitás tipikusan néhány pF:

| Cp | f_SRF (L = 10 µH) |
|---|---|
| 3 pF | ≈ 29 MHz |
| 5 pF | ≈ 22,5 MHz |
| 10 pF | ≈ 15,9 MHz |

Az SRF így a 400 kHz-es kapcsolási frekvenciához képest legalább 40-szeres (≥ 15 MHz), vagyis az alapharmonikus és az első harmonikusok bőven az induktív tartományban vannak. A kapcsolóél (tr ≈ 5 ns) spektruma azonban 0,35/tr ≈ 70 MHz-ig terjed, ami az SRF fölött van. Ebben a tartományban az induktor már nem szűr, a nagyfrekvenciás komponenseket a Cp átvezeti. Következmények a tervezésre:

1. **Az induktor nem helyettesíti a nagyfrekvenciás szűrést.** A HF komponenseket a bemeneti oldali C7 (100 nF), a kimeneti kerámia kondenzátorok és a szoros hurkú elrendezés kezeli.
2. **A kapcsolási csomópont (SW) és az induktor környezete csak kis, erősen lokalizált réz** legyen, mert a nagy dv/dt a Cp-n át közvetlenül a kimenetre csatolódhat.
3. **Árnyékolt kivitel** (CKST0503) csökkenti a külső mágneses és az elektromos tér csatolását.
4. **Ellenőrzés.** Az adatlapi SRF-et (a gyártói impedanciagörbét) a beszerzésnél rögzíteni, a prototípuson pedig LCR-mérővel vagy vektoros hálózatelemzővel (VNA) ellenőrizni kell (impedanciacsúcs, az SRF a negatív fázisszögű tartomány kezdete).

### 2.3.5 Visszacsatoló osztó (R2, R3)

A kimeneti feszültséget az FB bemenetre kötött ellenállásosztó állítja be, a belső referencia (Vref = 0,596 V) szerint *[T]*:

```
Vout = Vref * ( 1 + R2 / R3 ) = 0.596 * ( 1 + 45.3k / 10.0k ) = 0.596 * 5.53 = 3.296 V
```

| Elem | Érték | Tűrés |
|---|---|---|
| R2 (felső) | 45,3 kΩ | 1 % |
| R3 (alsó) | 10,0 kΩ | 1 % |
| Osztóáram | 3,296 V / 55,3 kΩ ≈ 59,6 µA | – |

**Tűrés-elemzés (csak az ellenállásokra, Vref névleges):**

```
Vout,max = 0.596 * (1 + (45.3k*1.01)/(10.0k*0.99)) = 0.596 * 5.6215 = 3.350 V
Vout,min = 0.596 * (1 + (45.3k*0.99)/(10.0k*1.01)) = 0.596 * 5.4403 = 3.242 V
```

A kimenet az ellenállás-tűrések miatt 3,24–3,35 V között mozoghat, a referenciafeszültség saját tűrése ezt tovább növeli. Ez az ESP32-C3-WROOM-02 3,0–3,6 V-os üzemi tartományán belül marad. Az osztóáram (≈ 60 µA) állandó terhelés, amely alvó módban a bemeneten kb. 17 µA-nek felel meg; az alvó üzemű áramfelvétel értékelésénél figyelembe kell venni.

### 2.3.6 Kimeneti szűrés (C6A1, C6B1)

A kimenetet két párhuzamosan kapcsolt 22 µF / 10 V, X7R, 0805 kondenzátor (C6A1, C6B1) simítja és pufferolja. A kimeneti ripple a kapacitív és az ESR-összetevőből adódik:

```
Delta_Vout ≈ Delta_IL / (8 * f_sw * C_eff) + Delta_IL * ESR

C_eff ≈ 25 uF (2 x 22 uF DC-előfeszítés miatt csökkentve),  ESR(2 db párhuzamosan) ≈ 2 mOhm

Vin = 18 V:  0.674 / (8 * 400e3 * 25e-6) + 0.674 * 0.002 = 8.4 mV + 1.3 mV ≈ 9.7 mVpp
Vin = 28 V:  0.728 / 80 + 0.728 * 0.002                  = 9.1 mV + 1.5 mV ≈ 10.6 mVpp
```

Mindkét szám a 15 mVpp-s követelmény alatt van. A kerámia kondenzátor kapacitása az egyenfeszültség-előfeszítéssel jelentősen csökken (X7R, 3,3 V-os működésnél tipikusan 30–50 %), ezért a méretezés a csökkentett értékkel történt. A 10 V-os feszültségkategória (a 3,3 V-os üzemi feszültség háromszorosa) az előfeszítés miatti kapacitáscsökkenést mérsékli.

**Bemeneti oldal.** A kapcsoló bemeneti árama impulzusszerű; az effektív értéke a 18 V-os bemenetnél I_in,rms = Iout · √(D(1−D)) = 0,6 · √(0,183 · 0,817) ≈ 0,23 A. Ezt a C7 (100 nF) nem tudja elnyelni, ezért a TPS54302 adatlapi referenciakapcsolása szerinti kerámia (legalább 10 µF) bemeneti kondenzátor szükséges a VIN lábhoz közvetlenül, a C7 ezt a nagyfrekvenciás oldalon egészíti ki. *[Ellenőrzendő a kapcsolási rajzon.]*

---

# 3. CAN ILLESZTŐ ÉS FIZIKAI RÉTEG (ISO 15765-4)

## 3.1 Az SN65HVD231 transceiver kiválasztásának indoklása

A kiválasztott áramkör a Texas Instruments **SN65HVD231**, 3,3 V-os CAN adó-vevő. Indoklás:

| Szempont | SN65HVD231 | 5 V-os transceiver (pl. TJA1050, MCP2551) |
|---|---|---|
| Tápfeszültség | 3,3 V (közvetlen a rendszersínről) | 5 V (külön sín vagy töltéspumpa szükséges) |
| Logikai szint az ESP32-C3-mal | Közvetlen, szintillesztés nélkül | Szintillesztés a 3,3 V-os RXD felé |
| Alkatrészszám / veszteség | Minimális | Többlet sín, többlet alkatrész, többlet fogyasztás |
| Standby fogyasztás | < 5 µA | Típusfüggő |
| Busz láb túlfeszültség-tűrés | −16 V … +16 V | Típusfüggő |

A busz-lábak −16 V … +16 V-os tűrése (a szabványos −2 … +7 V-os közös módú üzemi tartomány fölött) nem a tápoldali load dump ellen véd (az a 2. fejezet feladata), hanem a CAN-vezetékeken fellépő, a tápvezetéken át csatolt tranziensekkel, rövid zárlattal és a vezetékhiba állapotokkal szemben ad tartalékot. Nincs szükség 5 V-os kiegészítő sínre, szintillesztésre vagy töltéspumpára, ami az áramkör bonyolultságát, felületigényét és nyugalmi áramát csökkenti.

A buszfeszültségek szabványos értékei (ISO 11898-2): recesszív állapotban CANH ≈ CANL ≈ 2,5 V; domináns állapotban CANH ≈ 3,5 V, CANL ≈ 1,5 V (differenciális ≈ 2 V a 60 Ω-os eredő terhelésen).

## 3.2 Alacsony fogyasztású készenléti mód (RS láb)

Az SN65HVD231 8-as kivezetése (RS) az üzemmódot vezérli, és az ESP32-C3 egy GPIO-járól hajtható:

| RS szint | Üzemmód | Hatás |
|---|---|---|
| LOW (GND) | Nagysebességű (high-speed) | Adás és vétel normál, maximális meredekségű élekkel |
| HIGH | Készenléti (standby) | Adó kikapcsolva, a vevő alacsony fogyasztású figyelő üzemben; áramfelvétel < 5 µA |

**Cél.** Álló motornál és kikapcsolt gyújtásnál a diagnosztikai port 16-os lába általában közvetlenül az akkumulátoron van (állandó táp). Ha a modul folyamatosan adna és vezérelne rádiót, az akkumulátort lemerítené. Az áramfelvétel csökkentésének lépései:

1. A firmware a busz-aktivitás hiánya (pl. > 10 s) után az RS lábat HIGH szintre állítja (standby), a TWAI vezérlőt leállítja.
2. Az ESP32-C3-at deep sleep módba helyezi; ébresztő forrás a transceiver RXD kimenete (domináns bit → RXD LOW).
3. Busz-aktivitás (gyújtásbekapcsolás, CAN ébresztő keret) megjelenésekor az ESP32-C3 felébred, az RS lábat LOW-ra állítja, és elindítja a lekérdezési állapotgépet (4.2).

> **Megvalósítási megjegyzés.** Az ESP32-C3-nál csak a GPIO0–GPIO5 (RTC tartományú) lábak képesek deep sleep ébresztésre. Az RXD jelet ezért az ilyen lábra célszerű kötni, vagy ellenőrizni kell, hogy az adott pinkiosztás megengedi. Az áramfelvétel értéke a standby üzemben, az ESP32-C3 deep sleep és a buck nyugalmi árama együtt határozza meg *[B: ≤ 1 mA @ 12 V, mérendő]*. A napi fogyasztás 1 mA mellett 24 mAh, ami egy 60 Ah-s akkumulátor kapacitásának ≈ 0,04 %-a.

## 3.3 Busz-terminálás (JP2)

A J1962 portra csatlakozó CAN-hálózat gerincének mindkét végén 120 Ω-os lezárás található (a járműben beépített vezérlőkben). Az eredő terhelés ezért:

```
R_eredő = 120 || 120 = 60 Ohm        (két lezárás)
R_eredő = 120 || 120 || 120 = 40 Ohm (három lezárás – helytelen)
```

A modul a busz **végpontján kívül** csatlakozik (stub), ezért járműben a lezárást **nem szabad** hozzáadni:

| Környezet | JP2 állapota | Indoklás |
|---|---|---|
| Jármű | **Nyitva** | A hálózat már lezárt (60 Ω eredő); egy harmadik lezárás 40 Ω-ra csökkenti a terhelést, ami nagyobb adóáramot (domináns: ≈ 50 mA a ≈ 33 mA helyett 2 V differenciális szinten), kisebb jelamplitúdót és a többi csomópont adóterhelésének növekedését okozza |
| Tesztpad, szimulátor (egy lezárással) | **Összeforrasztva** | Ha a szimulátor csak egyik végen tartalmaz lezárást, a hálózat másik végét a modul 120 Ω-ja zárja le; így az eredő 60 Ω, és a reflexiók minimálisak |
| Tesztpad, szimulátor (két lezárással) | Nyitva | Mint járműben |
| Önálló kétcsomópontos próbapad | Mindkét végén 120 Ω | Mindkét végpont lezárása szükséges |

A forrasztóhíd (solder jumper) mérete és elérhetősége a hátoldalon (L4) szándékos: nincs szükség külön csatlakozóra vagy kapcsolóra, a beállítás egy forrasztással végrehajtható és visszabontható. Ellenőrzés: a CANH–CANL ellenállás mérése feszültségmentesen (6.1): nyitott JP2-vel nagy ellenállás (tízezer Ω nagyságrend, a transzceiver bemeneti impedanciája), zárt JP2-vel ≈ 120 Ω.

> **Megjegyzés (sebesség).** 500 kbit/s mellett a bitidő 2 µs; a modul belső vezetékhosszai (néhány cm) a jelterjedési idő (≈ 0,15 ns/mm) alapján elhanyagolhatóak, ezért a CAN-vezetékek impedancia-illesztése nem kritikus, a szimmetria és a rövidség a lényeges.

---

# 4. MIKROVEZÉRLŐ ÉS SZOFTVERES MŰKÖDÉS

## 4.1 Az ESP32-C3 hardveres képességei

A modul **ESP32-C3-WROOM-02** (4 MB Flash, antennaintegrált).

| Képesség | Érték / szerep |
|---|---|
| Mag | 32 bites RISC-V, 160 MHz |
| Háttértár | 4 MB Flash |
| Vezeték nélküli | 2,4 GHz Wi-Fi (ESP-NOW), Bluetooth LE |
| CAN vezérlő | **TWAI** (Two-Wire Automotive Interface): CAN 2.0B, 11 bites szabványos és 29 bites kiterjesztett azonosítók, hardveres szűrés |
| USB | Natív USB-Serial/JTAG vezérlő (D+/D−): firmware-feltöltés és naplózás külső illesztő IC nélkül |
| Üzemmódok | Normál, Listen-Only (csak figyelő), No-Ack/Self-test |

**TWAI konfiguráció.** 500 kbit/s-hoz a TWAI az ESP32-C3-nál 80 MHz-es APB órajelből indul: BRP = 8 (10 MHz, Tq = 100 ns), 20 Tq/bit (SYNC 1 + TSEG1 15 + TSEG2 4), SJW = 3. A mintavételi pont ≈ 80 %, ami a CAN ajánlott tartományában van. A válaszok hardveres szűrésére az ECU-válaszazonosítók (0x7E8…0x7EF) kiválasztása szolgál:

```c
// 11 bites azonosító: kód = 0x7E8, maszk: alsó 3 bit "don't care"
twai_filter_config_t f = {
    .acceptance_code = (0x7E8u << 21),
    .acceptance_mask = ~(0x7F8u << 21),   // 1 = nem vizsgált bit
    .single_filter   = true
};
```

Így a CPU-t csak a motorvezérlő (és társ-ECU-k) válaszkeretei terhelik, a busz többi forgalma hardveresen kiszűrődik. Az első bekapcsoláskor és járműben az első percben **Listen-Only** módot kell használni, hogy a modul ne zavarja a buszt (6.3).

## 4.2 OBD-II lekérdezési ciklus és állapotgép

**Kérés/válasz formátum (ISO 15765-4, 11 bites).** A funkcionális kérést a 0x7DF azonosítóra kell küldeni, 8 bájtos kerettel (a ki nem használt bájtok kitöltése tipikusan 0x55):

```
Kérés (RPM):    ID 0x7DF   DLC 8   [ 02 | 01 | 0C | 55 55 55 55 55 ]
                                      |    |    |
                                      |    |    PID
                                      |    Szolgáltatás (Mode 01: valós idejű adatok)
                                      ISO-TP PCI: egy keret (Single Frame), 2 hasznos bájt

Válasz (RPM):   ID 0x7E8   DLC 8   [ 04 | 41 | 0C | A | B | 55 55 55 ]
                                      |    |    |   |   |
                                      |    |    |   Adatbájtok
                                      |    |    PID visszhang
                                      |    Pozitív válasz (0x40 + 0x01)
                                      PCI: 4 hasznos bájt
```

**PID-táblázat:**

| PID | Paraméter | Adatbájt | Képlet | Tartomány | Mértékegység |
|---|---|---|---|---|---|
| 01 0C | Motorfordulatszám | 2 (A, B) | `RPM = (256·A + B) / 4` | 0 … 16 383,75 | min⁻¹ |
| 01 0D | Járműsebesség | 1 (A) | `v = A` | 0 … 255 | km/h |
| 01 05 | Hűtővíz-hőmérséklet | 1 (A) | `T = A − 40` | −40 … 215 | °C |
| 01 11 | Fojtószelep-állás (abszolút) | 1 (A) | `TPS = 100·A / 255` | 0 … 100 | % |

Indításkor a firmware a 01 00 PID-del (támogatott PID-ek bitmapja) lekérdezi, mely PID-eket támogatja a jármű, és csak azokat kérdezi. A nem támogatott paraméter a csomagban érvénytelenként jelölődik (4.3).

**Állapotgép:**

```
        +--------+     +--------+                +---------+
 RESET->|  INIT  |---->| STANDBY|--(CAN RXD/gyújt.)-->| WAKE   |
        +--------+     +---^----+                +----+----+
                           |                          |
              (buszcsend > 10 s)                  RS=LOW, TWAI start
                           |                          v
                      +----+-----+   OK     +---------+--------+
                      |  ERROR / |<---------|     BUS_SNIFF    |
                      | RECOVERY |  hiba    |  (listen-only)   |
                      +----^-----+          +---------+--------+
                           |                          | forgalom látható
                           |                          v
                           |                 +--------+--------+
                           |   timeout x N   |      PROBE      |  (01 00, 01 20 ...)
                           +-----------------+--------+--------+
                           |                          | PID-lista kész
                           |                          v
                           |                 +--------+--------+
                           +-----------------|  POLL_REQ  ->   |<------+
                             busz-off        |  WAIT_RESP      |       | következő PID
                                             |  PARSE -> TX    |-------+
                                             +-----------------+
```

| Állapot | Feladat | Kilépési feltétel |
|---|---|---|
| INIT | GPIO, TWAI konfiguráció, ESP-NOW peer felvétel | Inicializálás kész |
| STANDBY | RS = HIGH, deep sleep | RXD ébresztés |
| WAKE | RS = LOW, TWAI indítás Listen-Only módban | Busz-aktivitás észlelve |
| BUS_SNIFF | Forgalom figyelése, hibaszámlálók | Forgalom stabil → PROBE |
| PROBE | 01 00 … támogatott PID-ek felderítése | Lista kész |
| POLL_REQ → WAIT_RESP | Kérés küldése, válasz várása (P2 ≤ 50 ms) | Válasz, vagy timeout |
| PARSE → TX | Képlet alkalmazása, csomag összeállítás, ESP-NOW küldés | Küldés kész → következő PID |
| ERROR / RECOVERY | Bus-off kezelés, újraindítás, érvénytelen jelzők | Sikeres újraindulás |

**Ütemezés (a frissítési követelmény teljesítéséhez).** A kérés–válasz tranzakció busz-ideje elhanyagolható (egy 8 bájtos keret 500 kbit/s-on ≈ 0,22–0,26 ms), a meghatározó tényező az ECU válaszideje, amely járműtől és terheléstől függően tipikusan néhány–néhány tíz ms (a szabvány határa P2 ≤ 50 ms). A gyorsan változó paraméterek (fordulatszám, fojtószelep, sebesség) minden ciklusban, a lassú paraméter (hűtővíz-hőmérséklet) ritkábban (≈ 1 Hz) kerül lekérdezésre:

```
Ciklus: RPM -> TPS -> SPEED -> RPM -> TPS -> SPEED -> ... (minden ~50. ciklusban COOLANT)
ECU-válaszidő ≈ 8 ms esetén: 3 lekérdezés ≈ 24 ms -> RPM frissítés ≈ 40 Hz
```

A tényleges mintavétel ECU-függő; a 40–60 Hz-es megjelenítési rátát a kijelző oldali interpoláció (extrapoláció/simítás) biztosítja, a mintavételi cél legalább 30–40 Hz a gyorsan változó paraméterekre. Több PID egyetlen kérésben (Mode 01, legfeljebb 6 PID) szintén lehetséges, de a válasz ISO-TP többkeretes, és nem minden ECU támogatja; ez opcionális optimalizálás.

## 4.3 ESP-NOW adatcsomag-struktúra

A csomag fix hosszúságú, bináris, little-endian (a RISC-V natív bájtsorrendje), `packed` struktúra; nincs lebegőpontos adat, a konverziót a kijelző végzi a nyers értékekből.

```c
typedef struct __attribute__((packed)) {
    uint8_t  magic;         // 0xA5 – csomagazonosító
    uint8_t  version;       // protokollverzió (jelenleg 1)
    uint16_t seq;           // folyamatos sorszám (csomagvesztés-detektálás)
    uint32_t timestamp_ms;  // az adó idő (ms, bekapcsolás óta)
    uint16_t rpm_raw;       // (256*A + B)  -> RPM = rpm_raw / 4
    uint8_t  speed_kmh;     // A  -> km/h
    uint8_t  coolant_raw;   // A  -> T = coolant_raw - 40  [°C]
    uint8_t  throttle_raw;  // A  -> TPS = 100 * throttle_raw / 255  [%]
    uint8_t  flags;         // állapotjelzők (lent)
    uint16_t crc16;         // CRC-16/CCITT-FALSE az előző 14 bájtra
} obd_telemetry_t;          // összesen 16 bájt
```

| Mező | Bájt | Szerep |
|---|---|---|
| `magic` | 1 | Csomagtípus/szinkron (0xA5) |
| `version` | 1 | Kompatibilitás-ellenőrzés |
| `seq` | 2 | Csomagvesztés és sorrend ellenőrzése (a kijelző számolja a hiányt) |
| `timestamp_ms` | 4 | Mintavétel időbélyege az adatkorosság számításához |
| `rpm_raw` | 2 | Fordulatszám nyers értékben (1/4 min⁻¹ felbontás) |
| `speed_kmh` | 1 | Sebesség |
| `coolant_raw` | 1 | Hűtővíz-hőmérséklet nyers (+40 °C offsettel) |
| `throttle_raw` | 1 | Fojtószelep-állás nyers (0…255) |
| `flags` | 1 | Lásd lent |
| `crc16` | 2 | Hibadetektálás |

**`flags` bitjei:**

| Bit | Jelentés |
|---|---|
| 0 | CAN busz aktív |
| 1 | ECU válaszol |
| 2 | RPM érvényes |
| 3 | Sebesség érvényes |
| 4 | Hűtővíz érvényes |
| 5 | Fojtószelep érvényes |
| 6 | Adó bus-off / helyreállítás alatt |
| 7 | Tartalék |

**Működési szabályok:**

- **Unicast, rögzített csatorna.** A modul a kijelző MAC-címére küld; mindkét egység azonos Wi-Fi csatornán működik. A link-réteg nyugtázás (ACK) és az újraküldés a MAC szinten automatikus; **alkalmazás szintű újraküldés nincs**, mert a telemetria gyorsan elavul: mindig a legfrissebb adat a fontos.
- **Titkosítás.** Opcionális (PMK/LMK), a 16 bájtos csomagnál elhanyagolható többletterheléssel.
- **Csatornavesztés kezelése.** Ha a kijelző 250 ms-nál hosszabb ideig nem kap érvényes csomagot, a kijelzőn „nincs jel" állapot jelenik meg, és a mutatók biztonságos értékre állnak.
- **Átviteli késleltetés.** Egy csomag teljes kerete ≈ 60 bájt (fejléc + 16 bájt hasznos adat + FCS), ami 1 Mbit/s-nál ≈ 0,5–0,7 ms légi idő; az ACK-val együtt is jóval a 4 ms-os követelmény alatt marad *[B]*.

---

# 5. NYÁK KIALAKÍTÁS ÉS GYÁRTÁSTECHNOLÓGIA

## 5.1 Négyrétegű rétegfelépítés (JLCPCB JLC04161H-7628)

Gyártó szabványos, 4 rétegű, 1,6 mm vastag stackupot használunk, kizárólag szabványos átmenő furatos viákkal (nincs költséges vak/eltemetett via).

| Réteg | KiCad név | Funkció | Tipikus vastagság (réz / dielektrikum) |
|---|---|---|---|
| L1 (Top) | F.Cu | Felületszerelt alkatrészek, buck kapcsolási hurok (U2 SW → L1 rövid felület), passzívak | 35 µm (1 oz) |
| Prepreg 7628 | – | Dielektrikum L1–L2 | ≈ 0,21 mm |
| L2 (Belső 1) | In1.Cu | **Teljes, megszakítás nélküli FÖLDSÍK** | 15,2 µm (0,5 oz) |
| Core | – | Magdielektrikum L2–L3 | ≈ 1,065 mm |
| L3 (Belső 2) | In2.Cu | Tápelosztás: +12 V védett táp, +3,3 V rendszertáp | 15,2 µm (0,5 oz) |
| Prepreg 7628 | – | Dielektrikum L3–L4 | ≈ 0,21 mm |
| L4 (Bottom) | B.Cu | Szimmetrikus CAN differenciális érpár (CANH, CANL), forrasztóhidak (JP2), hátoldali földkitöltés | 35 µm (1 oz) |

*A vastagságok névleges értékek, a gyártási tűrések és a végső összvastagság (≈ 1,6 mm) miatt a JLCPCB aktuális stackup-táblázata az irányadó.*

| Via | Fúró | Gyűrű (pad) | Megjegyzés |
|---|---|---|---|
| Szabványos átmenő furatos | 0,3 mm | 0,6 mm | Konzervatív áramterhelhetőség ≈ 1 A/via; tápágon legalább 2 via |

## 5.2 Miért kritikus az L2 megszakítás nélküli földsíkja?

1. **Alacsony hurokinduktivitású visszatérő út (képsík-hatás).** A kapcsolóáram a SW csomóponton az L1-en folyik, és közvetlenül alatta, az L2 földsíkon tér vissza, mert az áram a legkisebb impedanciájú (induktivitású) utat választja. Mivel az L1–L2 távolság csak 0,21 mm, a hurok nagyon kicsi:

```
L_hurok ≈ mu0 * h * l / w          (szalagvezeték közelítés)

mu0 = 1.2566e-6 H/m, h = 0.21 mm, l = 5 mm, w = 2 mm:
L_hurok ≈ 1.2566e-6 * 0.21e-3 * 5e-3 / 2e-3 = 0.66 nH

Tüske: V = L * dI/dt ≈ 0.66 nH * (0.9 A / 5 ns) ≈ 0.12 V
Összehasonlításként 5 nH-s hurok (földsík nélkül, hosszabb visszatérő út): ≈ 0.9 V
```

2. **Zavarsugárzás csökkentése.** A kis hurok sugárzása arányos az `f² · A · I` szorzattal; a földsík a hurok A területét minimalizálja.
3. **Árnyékolás.** A teli L2 földsík elválasztja a zajos L1 kapcsolási csomópontot az L3 tápelosztástól és az L4 CAN érpártól, így csökkenti a kapacitív és induktív csatolást.
4. **Referenciasík az RF vonalhoz.** A rádiós modul antennájához és a tápvezetékek visszatéréséhez stabil referenciát ad.

**Következmények a megvalósításra:** az L2-n nem vezethetők jelnyomvonalak, nincs szakadás vagy hasítás a kapcsolóáram útvonalában (egy rés a visszatérő áramot kerülőútra kényszeríti, megnövelve a hurok területét); a viák anti-pad-jeit a kapcsolási csomópont alatt a lehető legkisebbre kell venni, és a GND-viák sűrűn (stitching) kötik össze az L1 és L4 földkitöltést az L2-vel.

## 5.3 Huzalozási szabályok

| Terület | Szabály / cél |
|---|---|
| **Buck kapcsoló hurok** | A VIN láb – C_in (C7 és a kerámia bemeneti kondenzátor) – GND láb hurok a lehető legrövidebb, cél: hurokterület ≲ 25 mm². Az SW csomópont → L1 összeköttetés rövid, a rézterület minimális (≲ 20 mm²), alatta csak teli földsík |
| **Földelés** | A C_in és C_out GND-padjeihez legalább 2–2 via az L2-re, közvetlenül a padnél |
| **Visszacsatoló ág (FB)** | Az R2/R3 osztó közvetlenül az FB láb mellett, az SW csomóponttól és az L1-től távol vezetve, rövid, nem árnyékolt, de a zajos zónákkal párhuzamosan nem futó nyomvonal |
| **L1 alatt** | Nincs jelvezeték az induktor alatt és közvetlen közelében |
| **CAN differenciális pár** | Az L4-en, szimmetrikusan vezetve (azonos hossz, ±1 mm, azonos távolság, nincs stub); a J1962 6-os (CANH) és 14-es (CANL) lábától az SN65HVD231-ig a lehető legrövidebb úton; az átmenő viák párban, szimmetrikusan |
| **CAN referenciasík** | A L4 szomszédos síkja az L3 (táp). A visszatérő áramnak a 3,3 V/12 V sík mentén a dekuplungkondenzátorokon át kell záródnia, ezért a CAN pár alatt az L3-on ne legyen szakadás, és a transceiver közelében földelő viák (stitching) és dekuplung kondenzátorok szükségesek |
| **Zajos és érzékeny területek elkülönítése** | A CAN nyomvonalak és az antennazóna legyen távol a buck SW csomóponttól, az L1-től; a tápágak ne fussanak át az antennazónán |
| **Viák** | Kizárólag átmenő furatos, 0,3 mm / 0,6 mm; nincs via-in-pad |
| **Antennazóna** | Az ESP32-C3-WROOM-02 modul nyomtatott F-antennája (Inverted-F) a kártya szélén túlnyúlik; az antenna alatt és körül minden rétegen réz- és vezetékmentes keepout zóna, a modul adatlapja szerinti méretekkel; a földkitöltés az antenna alatt megszakad |

> **Megjegyzés (CAN impedancia).** 500 kbit/s mellett a CAN nem igényel szigorúan 120 Ω-os differenciális impedanciájú vezetést; a cél a szimmetria és a szimmetrikus zajérzékenység, nem az impedancia pontos illesztése.

## 5.4 Mechanikai rögzítés és 3D nyomtatott ház

A modul háza vízszintesen kettéosztott, 3D nyomtatott kialakítású (PETG vagy ABS). A kialakítás jellemzői:

- A NYÁK **fészkekben** ül, az USB csatlakozó környékén illesztett támasztékokkal, amelyek az USB csatlakozó mechanikai terhelését (dugózás) a házra vezetik, nem a forrasztási pontokra.
- **Egyetlen M3-as csavar** fogja össze a ház két felét és rögzíti a nyákot; a fészkek és a nyák körvonala megakadályozza az **elfordulást** a csavar körül.
- A J1962 csatlakozó az egyik oldalon kilép a házból; mellette az USB nyílás hozzáférhető marad a firmware-frissítéshez.
- Illesztési tűrések: FDM nyomtatásnál 0,2–0,3 mm hézag a nyák körül, falvastagság legalább 1,6–2 mm.
- Az antennazóna körül nincs fém (sem betét, sem csavar), a műanyag réteg közvetlenül az antenna fölött vékony és egyenletes.

> **Anyagválasztás.** Az utastér (különösen a műszerfal környéke, napsütésben) elérheti a 80–85 °C-ot. A PETG üvegesedési hőmérséklete ≈ 80 °C körüli, az ABS/ASA ≈ 100 °C feletti, ezért tartós járműbeli használatra az **ABS vagy ASA** javasolt; a PETG prototípusokhoz és mérsékelt hőmérsékletű felhasználáshoz megfelelő. A modul hődisszipációja kicsi (átlag ≈ 0,4–0,5 W), külön szellőzés nem szükséges.

---

# 6. ÉPÍTÉSI, ÉLESÍTÉSI ÉS MÉRÉSI ÚTMUTATÓ

## 6.1 Feszültségmentes ellenőrzés (zárlatvizsgálat)

A beültetés után, **tápfeszültség nélkül**, multiméterrel az alábbi pontokat kell ellenőrizni. Hibás értéknél a bekapcsolás tilos.

| Mérési pont | Várt eredmény |
|---|---|
| +3V3 ↔ GND | > 1 kΩ (a kondenzátorok töltődése miatt emelkedő érték); < 100 Ω zárlatgyanús |
| +12V_PROT ↔ GND | > 1 kΩ, emelkedő; < 100 Ω zárlatgyanús |
| F1 folytonossága | < 2 Ω |
| D1, D4 (diódaüzem) | D1: nyitóirányban ≈ 0,3–0,5 V; D4: záróirányban nincs vezetés, nyitóirányban ≈ 0,5–0,7 V |
| CANH ↔ CANL | JP2 nyitva: > 10 kΩ; JP2 zárva: ≈ 120 Ω |

## 6.2 Első bekapcsolás áramkorlátozott labortápról

A labortáp 12 V-ra, **50 mA-es áramkorláttal** állítva, JP2 nyitva, USB nélkül kapcsolandó a kártyára. Az áramfelvétel programozatlan modulnál ≈ 10–40 mA; ha a táp az áramkorlátba fut, azonnal le kell kapcsolni. Ezután ellenőrizendő:

| Mérés | Elvárt érték |
|---|---|
| +3,3 V sín | 3,30 V ± 0,05 V (a 6,5–18 V-os bemeneti tartományban végig) |
| SW csomópont | 400 kHz, tiszta négyszögjel |
| Kimeneti ripple | < 15 mVpp (20 MHz sávszélesség, rugós földelés) |

Ezt követően az USB-n keresztül a firmware feltölthető, és az ESP32-C3 a bootnaplóval azonosítható.

## 6.3 CAN busz kommunikációs tesztelés

### 6.3.1 Gyors teszt tesztpadon

A modul egy ECU-szimulátorra vagy 500 kbit/s-ra állított USB–CAN adapterre csatlakozik, amely a 0x7DF kérésekre 0x7E8 azonosítóval válaszol. A JP2 a szimulátor lezárásától függ (3.3): egyetlen lezárás esetén összeforrasztandó, kettőnél nyitva marad. A teszt menete:

1. A modul Listen-Only módban fogadja a szimulátor kereteit hibamentesen.
2. Aktív módban a kérésekre (01 0C, 0D, 05, 11) a válaszok helyesen dekódolódnak. Ismert bemenettel ellenőrizhető: A = 0x1A, B = 0xF8 esetén RPM = (256·26 + 248) / 4 = 1726 min⁻¹.
3. A kijelző (vagy egy ESP-NOW vevő tesztalkalmazás) hibamentesen fogadja a csomagokat: a `seq` mező folytonos, a CRC helyes.

### 6.3.2 Egyszerű teszt járműben

A tesztet **álló járműben**, kéziféket behúzva kell végezni, JP2 **nyitva**. A modul a J1962 portra csatlakoztatandó, majd gyújtás ráadása után először Listen-Only módban 1 percig figyelni kell a buszt (a hibaszámlálók nem növekedhetnek). Ezután bekapcsolható az aktív lekérdezés: motorindítás után a fordulatszámnak és a hűtővíz-hőmérsékletnek egyeznie kell a műszerfal értékeivel, gázadásra pedig a kijelzőn a fordulatszám és a fojtószelep-állás késedelem nélkül követi a változást. Gyújtáskikapcsolás után a modulnak standby üzemmódba kell lépnie.

---

## Függelék: Átdolgozási előzmények

| Rev. | Dátum | Leírás |
|---|---|---|
| A | 2026-10-07 | Első kiadás: teljes hardver- és firmware-architektúra, mérési útmutató |

*A dokumentum a megadott tervezési háttéranyagból (kapcsolási és NYÁK-paraméterek) és mérnöki számításokból készült. A [B] jelölésű értékek becslések, amelyeket a 6. fejezet szerinti méréssel kell igazolni; a 2.2.4 pontban felsorolt nyitott kérdéseket a prototípus-vizsgálat során le kell zárni.*
