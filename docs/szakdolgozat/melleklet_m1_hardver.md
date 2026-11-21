# M1. melléklet: Hardveres háttér és számítások

A melléklet a 1. és 2. fejezetből kivett részleteket tartalmazza: a szabványok és a NYÁK-tervezés elméleti hátterét, a méretezési számításokat, a kiosztási és rétegtáblázatokat, valamint a házak tervezési szempontjait. A főszöveg csak az eredményt és a döntést mondja ki; a levezetés itt található. A képletek számozása a mellékleten belüli (M1.1, M1.2, …). A szögletes zárójeles számok az irodalomjegyzék tételeire mutatnak, az azonosítók (H-n, V-n) a fejlesztési napló bejegyzéseire.

## M1.1. A CAN-busz működésének részletei

**Differenciális jelátvitel.** A busz két vezetékből áll (CANH és CANL). Recesszív állapotban mindkettő passzívan kb. 2,5 V-ra van előfeszítve, domináns állapotban a CANH kb. 3,5 V-ra emelkedik, a CANL kb. 1,5 V-ra süllyed, így a differenciális feszültség kb. 2 V [26]. A domináns bit „felülírja” a recesszívet, a buszon a logikai szint a meghajtó bemenetéhez képest invertált [26]. A differenciális átvitel miatt a közös módú zavarok kioltódnak, ami a gépjármű zajos környezetében alapvető előny.

**Arbitráció.** Az üzenet azonosítója határozza meg a prioritást: a kisebb bináris azonosító nagyobb prioritást jelent. Az arbitráció bitenkénti és roncsolásmentes: ha egy csomópont recesszív bitet küld, de a buszon dominánst olvas, visszalép és később próbálkozik újra [26]. Ennek köszönhetően a buszon nem keletkezik ütközés, és a nagy prioritású üzenetek késleltetése korlátos.

**Keretszerkezet.** A szabványos keret 11 bites, a kiterjesztett 29 bites azonosítót használ (2^11 = 2048, illetve 2^29 ≈ 537 millió azonosító) [26]. A keret mezői: keretkezdő bit (SOF), azonosító, RTR, IDE, foglalt bit, 4 bites adathosszkód (DLC), adat (legfeljebb 8 bájt), 15 bites CRC, nyugtázó mező (ACK) és keretvég (EOF) [26]. A diagnosztikai kommunikációban a kérés és a válasz mindig ilyen keretekben utazik.

[Ábra helye: A szabványos CAN adatkeret felépítése (saját szerkesztés a [26] és [4] alapján); forrás: a CAN-keret ábrája: a v0.5 vázlat 2. ábrája]

**Hibakezelés és bitbeszúrás.** Öt egymás utáni azonos szintű bit után a küldő egy ellentétes szintű bitet szúr be (bit stuffing), amelyet a vevő eltávolít; ez az órajel-szinkronizációhoz szükséges éleket biztosítja [26]. A hibákat CRC, nyugtázás, formaellenőrzés, bitfigyelés és a bitbeszúrás ellenőrzése fedezi fel. A hibás csomópont a hibaszámlálók segítségével fokozatosan lekapcsolódik a buszról, így egyetlen hibás egység nem tudja tönkretenni a hálózatot [26].

**Átviteli idő.** Egy sm adatbájtot hordozó szabványos keret hossza a keretközi térrel együtt bitbeszúrás nélkül 47 + 8·sm bit, a legrosszabb esetben, a beszúrt bitekkel együtt, a [4] szerint 55 + 10·sm bit. Nyolc adatbájtnál ez 111, illetve 135 bit. Az ISO 15765-4 diagnosztikai profilja 500 kbit/s (vagy 250 kbit/s) sebességet használ [FORRÁS KELL: az ISO 15765-4 megfelelő pontja]. A keret átviteli ideje így:

    tkeret = Nbit / fbit = 111…135 / 500 kbit/s = 0,222…0,270 ms        (M1.1)

Ez az érték lényegesen kisebb az ECU válaszidejénél, ezért a lekérdezés sebességét nem a busz, hanem az ECU szabja meg.

**Lezárás.** Az ISO 11898 szerinti topológia egyetlen sodrott érpár, amelynek mindkét végén 120 Ω-os lezáró ellenállás van, az érpár hullámimpedanciájának megfelelően. A lezáró ellenállás nem kerülhet egy csomópontra, mert ha a csomópontot lecsatlakoztatják, a busz elveszíti a lezárását [26]. A jármű hálózata már le van zárva, az OBD-egység leágazásként csatlakozik, ezért a modulban nem lehet fix lezárás.

## M1.2. Az OBD-II alkalmazási és szállítási rétege

[Ábra helye: Az OBD-II diagnosztika szabványrétegei (saját szerkesztés a [25], [16], [17] és [15] alapján); forrás: a rétegek ábrája: a v0.5 vázlat 3. ábrája]

**Szolgáltatások és PID-ek.** Az SAE J1979 (az ISO 15031-5 megfelelője) a diagnosztikai szolgáltatásokat (Mode) és az azokhoz tartozó paraméterazonosítókat (PID) írja le [25]. A dolgozatban használt szolgáltatások: a 01-es a valós idejű adatok olvasására, a 03-as a hibakódok kiolvasására, a 09-es a járműinformációk (például a gyártási szám, a VIN) lekérésére. A hibakódok törlése (04-es szolgáltatás) a dolgozat hatókörén kívül esik.

**M1.1. táblázat: A dolgozatban használt Mode 01 PID-ek és számítási képleteik [25]**

| PID | Paraméter | Képlet | Mértékegység |
|---|---|---|---|
| 01 0C | Motorfordulatszám | (256·A + B) / 4 | min-1 |
| 01 0D | Járműsebesség | A | km/h |
| 01 05 | Hűtővíz-hőmérséklet | A − 40 | °C |
| 01 11 | Fojtószelep-állás | 100·A / 255 | % |
| 01 42 | Vezérlőegység-feszültség | (256·A + B) / 1000 | V |

**Azonosítók és időzítés.** A kérést a funkcionális 0x7DF azonosítóra kell küldeni, amelyre az ECU-k 0x7E8…0x7EF azonosítójú keretekkel válaszolnak; ha egy adott vezérlőegységet kell megszólítani, a fizikai kérés azonosítója 0x7E0…0x7E7 [16]. Az ECU válaszideje a szabvány szerint legfeljebb 50 ms [FORRÁS KELL: a P2 időzítés pontos hivatkozása]. A támogatott PID-ek a 00, 20, 40 PID-ek által visszaadott bittérképből derülnek ki, ezért a lekérdezést ezzel kell kezdeni.

**Szállítási réteg (ISO-TP).** A CAN-keret legfeljebb 8 bájt adatot hordoz, ennél hosszabb üzenetek (például a 17 karakteres VIN) szállítására az ISO 15765-2 szerinti szállítási protokoll szolgál [17]. Az első bájt (PCI) a keret típusát jelöli: egyetlen kerettel átvihető üzenet, többkeretes üzenet első kerete, folytatókeret, és a vevő által küldött flow control keret, amely a folytatókeretek ütemét szabályozza. A kérések és válaszok többsége egyetlen keretben elfér; a VIN lekéréséhez (Mode 09, PID 02) többkeretes átvitelre és flow control kezelésére is szükség van.

**A gyártási szám.** A VIN 17 karakteres azonosító, amelynek első három karaktere a gyártó világazonosítója (WMI) [18]. Ebből ismeri fel a rendszer a gyártót, és ennek alapján a kijelzőn megjelenítendő emblémát.

**Az ELM327 soros átviteli ideje.** Az ELM327-alapú adapterek a soros vonalon 9600 vagy 38400 baud sebességgel, ASCII-karakterekkel kommunikálnak [5]. Egy rövid válasz átviteli ideje:

    tsoros = Nkar · 10 / fbaud = 14 · 10 / 38400 ≈ 3,6 ms        (M1.2)

ahol 10 az egy karakter átviteléhez szükséges bitek száma (start, 8 adat, stop bit), Nkar egy rövid válasz (például „41 0C 1A F8” és a lezáró karakterek) hossza. Egy-egy kérésnél ez önmagában nem sok, de ha a rendszer több paramétert kérdez le másodpercenként többször, a soros vonal és az adapter belső feldolgozása a lekérdezési sebességet korlátozza.

## M1.3. A NYÁK-tervezés elméleti háttere

A rendszer két saját tervezésű nyomtatott áramköri lapot (NYÁK) tartalmaz. A téma tág, ezért csak azok az elvek szerepelnek, amelyek a projektben ténylegesen szükségesek voltak.

**A NYÁK mint elektromos rendszer: rétegek és visszatérő áram.** A NYÁK nem egyszerű mechanikai hordozó, hanem az áramkör része: a vezetékek induktivitással, kapacitással és ellenállással rendelkeznek, és ezek nagyfrekvencián a jelek viselkedését meghatározzák [20, 23]. Egy jel áramának mindig vissza kell térnie a forrásához, és kis frekvencián ez a legkisebb ellenállású úton történik, nagy frekvencián viszont a legkisebb induktivitású (a legkisebb hurokterületű) úton, vagyis közvetlenül a jelvezeték alatti síkon [20, 23]. A hurokinduktivitás szalagvezeték-közelítéssel:

    Lhurok ≈ µ0 · h · l / w        (M1.3)

ahol µ0 a vákuum permeabilitása, h a vezeték és a visszatérő sík távolsága, l a hurok hossza, w a vezeték szélessége. A képletből az következik, hogy a hurok induktivitása a síktávolsággal arányosan csökken, ezért a jel- vagy kapcsolóáram alá helyezett, megszakítatlan földsík és a minél vékonyabb dielektrikum csökkenti a legjobban a hurkot. Ez az alapja a négyrétegű rétegfelépítésnek, amelyben a második réteg szilárd földsík. Az Espressif is ezt javasolja az ESP32-S3 köré: a felső réteg alkatrészeknek és jeleknek, a második réteg megszakítatlan föld, a harmadik tápelosztásnak [9].

[Ábra helye: A visszatérő áram útja megszakítatlan és megszakított földsík esetén (saját szerkesztés); forrás: a v0.5 vázlat 4. ábrája]

**Kapcsolóüzemű tápegység elrendezése.** A lépcsős (buck) szabályozóban a legnagyobb áramváltozás a bemeneti kondenzátor, a kapcsolók és a föld alkotta „forró hurokban” zajlik. A Texas Instruments elrendezési útmutatója szerint ennek a huroknak a vezetékei legyenek szélesek és rövidek, a hurokterület minimális, a bemeneti kondenzátor a VIN és GND lábhoz a lehető legközelebb kerüljön [27]. A kapcsolási csomópont (SW) rézfelülete az induktorig rövid és széles legyen, de összterülete minimális, mert a nagy dv/dt miatt ez a sugárzás fő forrása. A visszacsatoló osztó az FB lábhoz közel, a zajforrásoktól távol helyezkedjen el. Legalább egy földsík szükséges egy belső rétegen az árnyékolás és a hőelvezetés miatt [27].

**Nagyfrekvenciás és differenciális jelek.** Egy vezeték akkor viselkedik távvezetékként (elektromosan hosszúként), ha a jel terjedési ideje összemérhető a jel felfutási idejével. Szokásos közelítés szerint elektromosan hosszúnak tekinthető, ha

    tpd > tr / 6        (M1.4)

ahol tpd a jel terjedési ideje a vezetéken, tr a jel felfutási ideje [20]. Ilyenkor a vezeték lezárása és impedanciája már számít, különben visszaverődések keletkeznek. A differenciális párok (például az USB D+/D−) esetén a szimmetria, az azonos hossz és a megszakítatlan referenciasík a fontos: az Espressif az USB-párra 90 Ω-os differenciális impedanciát, hosszillesztést, kevés via-t és folytonos földreferenciát ír elő [9]. A kijelző QSPI-vonalainál a hosszeltérés (skew) számít, a vonalak között és az órajelhez képest.

**Rádiófrekvenciás rész és az antenna.** A beépített nyomtatott antennás ESP32-modul akkor működik jól, ha az antenna a kártya szélén túlnyúlik, és körülötte, valamint alatta nincs réz és vezeték. Az Espressif útmutatója szerint az antennának a kártyaélen túl kell nyúlnia, a ház anyagától pedig legalább 15 mm távolságot kell tartani, és az antenna közelében nem szabad nagy sebességű jelet (USB, óra) vezetni [9].

**Védelem, gyárthatóság és a hibák megelőzése.** A gépjármű fedélzeti hálózata nem stabil táp: a feszültség tranzienseket tartalmaz, ezért a bemeneten védelem (biztosító, TVS-dióda, fordított polaritás elleni dióda) szükséges. A tervezést a gyártási eljárás is korlátozza: a gyártó (JLCPCB) a szabványos rétegfelépítést, a minimális vezetékszélességet és -távolságot, a furatokat és az összeszerelési peremsínt előírja [19]. A kapcsolási rajz és a NYÁK ellenőrzése a gyártás előtt egyszerűbb, mint a hiba utólagos javítása, mert a gyártásba adott rendelésben az alkatrészek már nem módosíthatók [19].

**M1.2. táblázat: A NYÁK-tervezési alapelvek és alkalmazásuk helye a dolgozatban**

| Alapelv | Fő összefüggés | Alkalmazás a dolgozatban |
|---|---|---|
| Visszatérő áram, földsík | hurokinduktivitás (M1.3) | 2.5.1. pont, mindkét NYÁK |
| Kapcsolóüzemű tápegység elrendezése | kis „forró hurok”, kis SW-felület | 2.3.2., 2.5.2. pont |
| Elektromosan hosszú vezeték | (M1.4) kritérium | M1.6 (QSPI) |
| Differenciális pár | 90 Ω, hosszillesztés | M1.6 (USB) |
| Antenna | kártyaél, szabad zóna | 2.5.2., 2.6. pont |
| Védelem és gyárthatóság | TVS, peremsín, DFM | 2.3.1., 2.7. pont |

## M1.4. Az OBD-egység méretezése

**A védelem és a TVS-dióda.** Az OBD-egység fő tápforrása a J1962 csatlakozó 16-os lába. A gépjármű-ág bemenetén az SMAJ24A TVS-dióda (D4) véd a tranzienseken, az USB-ág sorosan egy visszaálló biztosítót (F1, PPTC) tartalmaz, a két forrást diódás VAGY-kapcsolás (D8, SS34 és D2, SS14 Schottky-diódák) választja szét. A TVS-dióda kapocsfeszültségét a lépcsős szabályozó (TPS54302) határadataival kellett összevetni. Az SMAJ24A tipikus kapocsfeszültsége nagy impulzusáramnál 38,9 V, míg a szabályozó bemenetének abszolút maximuma 30 V, az ajánlott legnagyobb üzemi feszültsége pedig 28 V [30]. A dióda tehát a szabályozót a jelentős energiájú tranziensek (például load dump) ellen nem védi teljesen. A NYÁK gyártás után nem módosítható; normál 12 V-os hálózatban a kockázat elfogadható (H-09), a Rev. B-ben nagyobb védőképességű TVS-t vagy a szabályozó elé kapcsolt előszűrőt kell tervezni. [A diódaadatok ellenőrzése a gyártói adatlapból a szerző feladata.]

**A 3,3 V-os lépcsős szabályozó.** A TPS54302 szinkron lépcsős (buck) szabályozó 400 kHz-es kapcsolási frekvencián állítja elő a 3,3 V-os rendszersínt [30]. Az LDO a 12 V-ról (akár 18 V-ról töltésnél) a nagy veszteség miatt nem volt ésszerű: az ESP32-C3 rádiós áramcsúcsai mellett a disszipáció a kis tokban tűrhetetlen lenne. A kimeneti feszültséget az FB bemenetre kötött osztó (R2 = 45,3 kΩ, R3 = 10,0 kΩ) állítja be a 0,596 V-os belső referencia szerint [30]:

    Uki = Uref · (1 + R2 / R3) = 0,596 V · (1 + 45,3 kΩ / 10,0 kΩ) ≈ 3,296 V        (M1.5)

Az ellenállások ±1 %-os tűrése mellett a kimenet 3,242 V és 3,350 V között mozoghat, ami az ESP32-C3-WROOM-02 3,0–3,6 V-os üzemi tartományán belül marad [7].

**Az induktivitás megválasztása.** Két értéket vetettem össze:

**M1.3. táblázat: A 10 µH és 33 µH közötti választás szempontjai (Ube = 18 V)**

| Szempont | 10 µH | 33 µH |
|---|---|---|
| Ripple-áram | 0,674 A | 0,204 A |
| Csúcsáram | 0,937 A | 0,702 A |
| Terhelési ugrás utáni követés | gyorsabb | lassabb |
| Kimeneti ripple (becsült) | ≈ 8–10 mV (csúcstól csúcsig) | ≈ 3 mV (csúcstól csúcsig) |
| Méret, DC ellenállás | kisebb | nagyobb |

A 10 µH-s értéket választottam. Ezzel a rádió bekapcsolásakor jelentkező terhelési ugrást a szabályozó gyorsabban követi, a kimeneti ingadozás a megkívánt 15 mV (csúcstól csúcsig) alatt marad, az induktor csúcsárama pedig a megengedett érték alatt van. Az ellentételezett hátrány a nagyobb ripple-áram miatti nagyobb zavarkibocsátás, amelyet a bemeneti szűrés és a gondos elrendezés kezel. A kimeneti ripple becslése a kapacitív és az ESR-összetevőből:

    ΔUki ≈ ΔIL / (8 · fsw · Ceff) + ΔIL · RESR        (M1.6)

A két párhuzamosan kötött 22 µF-os kerámia kondenzátor (C6A1, C6B1) egyenfeszültség-előfeszítés miatt csökkent, becsült effektív kapacitása 25 µF, eredő ESR-értéke kb. 2 mΩ. Ezekkel 18 V bemenetnél ΔUki ≈ 8,4 mV + 1,3 mV ≈ 9,7 mV adódik, ami a 15 mV-os követelmény alatt van. Az értékek számítási becslések; a tényleges ripple mérése oszcilloszkóp hiányában nem készült el.

**A TWAI időzítése.** A TWAI időzítése 500 kbit/s-hoz az APB 80 MHz-es órajeléből adódik:

    fbit = 80 MHz / (BRP · NTq) = 80 MHz / (8 · 20) = 500 kbit/s        (M1.7)

ahol BRP az előosztó (8), NTq pedig a bitenkénti időkvantumok száma (20: 1 szinkron, 15 TSEG1 és 4 TSEG2). A mintavételi pont így a bit 80 %-ánál van, ami a CAN ajánlott tartományába esik [15].

**M1.4. táblázat: Az OBD-egység fontosabb GPIO-kiosztása**

| GPIO | Jel | Funkció |
|---|---|---|
| IO4 | CAN_TX | Az adóvevő TXD bemenete (TWAI TX) |
| IO5 | CAN_RX | Az adóvevő RXD kimenete (TWAI RX) |
| IO10 | OPMODE | Az adóvevő RS lába |
| IO6, IO7, IO8 | LED | Állapotjelző LED-ek (zöld, kék, piros) |
| IO9 | BOOT | Betöltő-mód gomb |
| IO18, IO19 | USB D−, D+ | Natív USB-Serial/JTAG |
| IO20, IO21 | UART | Tesztpadok |

## M1.5. A kijelzőegység méretezése

**A QSPI képátvitel ideje.** A QSPI négy adatvonalán a teljes képkocka átviteli ideje az órajeltől függően:

    tkocka = (W · H · b) / (4 · fCLK) = (360 · 360 · 16) / (4 · 40 MHz) ≈ 12,96 ms        (M1.8)

ahol W és H a kijelző méretei, b a színmélység (16 bit), fCLK pedig a QSPI órajele. Az ESP32-S3 GPIO-mátrixon át vezérelt QSPI-jánál az órajel megbízható felső határa 40 MHz körüli, ez elméletileg legfeljebb ≈ 77 képkocka/másodpercet tesz lehetővé a teljes képernyő átírásánál. A 30–60 képkocka/másodperces cél tehát teljesíthető, ha az átvitel DMA-val a processzortól függetlenül fut. A képkocka mérete 16 bites színmélységnél 360 · 360 · 2 B = 259 200 B ≈ 253 KiB.

**Az érintő I²C-vonala.** A kártyán nincsenek külső I²C felhúzó ellenállások. A Waveshare modul kapcsolási rajzán a szinteltoló mindkét oldalán 4,7 kΩ-os felhúzók vannak az SDA/SCL vonalakon (R7, R14, R9, R11); a rajz első átnézése ezeket tévesen nem találta meg (H-06). Az érintő működésével sem volt probléma, külső felhúzó nem kell.

**Töltőáram.** A TP4056 töltőáramát a PROG láb ellenállása (R18) állítja be [22]:

    Itöltés = 1200 / RPROG[kΩ] mA = 1200 / 2,4 = 500 mA        (M1.9)

Egy 1000 mAh-s cellánál a töltési idő a 0,5 A-es töltőárammal, a CV-szakaszt is számítva, a tervezés szerint kb. 2,4 óra. A cella kapacitásának 0,5 C-s töltése a gyártók által általában megengedett tartományban van, a konkrét cella adatlapján ellenőrzendő.

**A tápútválasztó MOSFET kapcsolási késleltetése.** A Q1 (AO3401A, P-csatornás) gate-hálózatának időállandója:

    τ = R17 · C10 = 100 kΩ · 10 µF = 1,0 s        (M1.10)

A FET akkor kapcsol be, amikor a gate feszültsége a source alá legalább |VGS(th)| ≈ 1 V-tal esik. A D8 lezárásakor a +5V hálózat kb. 3,4 V-ról indul, a küszöb 3,05 V − 1,0 V ≈ 2,05 V, így

    tbe = τ · ln(3,4 / 2,05) ≈ 0,5 s        (M1.11)

Ez idő alatt a terhelést a Q1 testdiódája (body diode) viszi, amely nanoszekundumos nagyságrendű idő alatt (becsült érték) nyit, és a VSYS átmenetileg VBAT − 0,65 V-ra esik. A rendszer akkor marad zavartalan, ha ez az érték az ESP32-S3 brown-out küszöbe (≈ 2,8 V) fölött marad, azaz VBAT ≳ 3,55 V. Mélyebben kisütött celláknál USB kihúzásakor újraindulás várható. A javítás az R17-re párhuzamosan forrasztott 1 kΩ-os ellenállás, amellyel az idő ≈ 5 ms-ra csökken (a mellékhatás kb. 4 mA állandó terhelés USB-üzemben); Rev. A-n ez nem készült el (H-02).

**A 3,3 V-os LDO termikus terhelése.** A TLV75733 fix 3,3 V-os, kis kiesési feszültségű lineáris szabályozó, 1 A-es kimeneti áramkapacitással [29]. Az ESP32-S3 rádiós áramcsúcsai becslésem szerint 0,5 A alatt maradnak. A disszipáció és a tok hőmérséklet-emelkedése:

    Pveszt = (Ube − Uki) · I,  ΔT = Pveszt · θJA        (M1.12)

Akkumulátoros üzemben Ube = 3,7 V és I = 250 mA mellett P = 0,1 W, ΔT ≈ 20 °C (θJA ≈ 200 °C/W a SOT-23-5 tokra). USB-n a három soros elem (F1, D5, D8) miatt az Ube ≈ 4,1 V, ezért P = 0,2 W és ΔT ≈ 40 °C. A megengedett 125 °C-os átmeneti hőmérséklethez ez 85 °C-ig megengedett környezeti hőmérsékletet jelent, ami a napsütéses műszerfalnál elégséges, de csökkenő tartalékkal. A veszteség csökkentésére a tokhoz nagy rézfelület és hővezető viák tartoznak, a szoftver pedig USB-üzemben korlátozhatja a háttérvilágítás fényerejét.

## M1.6. A NYÁK-ok kialakítása

**M1.5. táblázat: A négyrétegű NYÁK rétegkiosztása (JLCPCB JLC04161H-7628, 1,6 mm [19])**

| Réteg | Funkció |
|---|---|
| L1 (felső) | Alkatrészek, kapcsolási hurkok, nagyfrekvenciás jelek |
| L2 (belső 1) | Megszakítatlan földsík |
| L3 (belső 2) | Tápelosztás, lassú jelek |
| L4 (alsó) | OBD: CAN-érpár; kijelző: nyomógombok, akkumulátor-zóna |

**A hurokinduktivitás.** Az L2 megszakítatlan földsík azért kulcsfontosságú, mert a visszatérő áram a jelút alatt záródik, így az áramhurok kicsi. A kapcsolóáram hurokinduktivitása az (M1.3) szerint:

    Lhurok ≈ µ0 · h · l / w = 1,2566·10-6 · 0,21·10-3 · 5·10-3 / (2·10-3) ≈ 0,66 nH        (M1.13)

ahol h az L1–L2 dielektrikum vastagsága (0,21 mm), l a hurok hossza (5 mm), w pedig a vezeték szélessége (2 mm). Ez a lépcsős szabályozó 0,9 A-es, 5 ns-os áramugrásánál kb. 0,12 V feszültségtüskét ad, szemben a földsík nélküli, 5 nH-s hurokra becsült kb. 0,9 V-tal.

**Az OBD-egység NYÁK-ja.** A CAN differenciális érpár az L4 rétegen halad, szimmetrikusan (azonos hossz ±1 mm, stub nélkül). A lépcsős szabályozó be- és kimeneti kondenzátorai, valamint az induktor a lehető legkisebb hurokterülettel kerültek elhelyezésre, az SW csomópont rézfelülete minimális. A modul antennás része túllóg a kártyaélen, és az antenna körül minden rétegen réz- és vezetékmentes zóna van. A hátoldali beültetésű alkatrészek (SN65HVD231 és passzívjai) miatt az 1-es láb irányát külön ellenőrizni kellett.

**A kijelzőegység NYÁK-ja.** A kártya kör alakú, három M2-es rögzítőfurattal, a hátoldalán 40,5 × 30 mm-es, alkatrészmentes akkumulátor-zónával. Az átlós méret (≈ 50,4 mm) adja meg a kör átmérőjének alsó határát, a gyakorlatban ≥ 52 mm. A kijelző QSPI-vonalai kb. 21 mm hosszúak, a hosszeltérésük legfeljebb ≈ 0,2 mm; ez 40 MHz-en bőven elegendő. Hogy a vonal elektromosan hosszúnak tekintendő-e, az (M1.4) kritérium dönti el: a kb. 21 mm-es nyomvonal terjedési késleltetése (≈ 130 ps) az 1–2 ns-os jelélekhez képest a határeset tartományába esik, ezért a vonalak rövid és egyenes kialakítása indokolt. Az USB D+/D− pár célimpedanciája 90 Ω, a mikroszalag-közelítő képlettel a választott 0,25 mm-es vezetékszélességgel és 0,15 mm-es távolsággal ≈ 92 Ω adódik; ez tájékoztató érték, a végleges geometriát a gyártó impedanciaszámítója határozza meg [19].

[Ábra helye: az alsó oldali szerelési nézetek (docs/00_kepek/Produce_DanZhi.SMT_Snapshot.Bottom.*) mindkét NYÁK-ról; a felülnézetek a főszöveg 3. ábráján vannak.]

## M1.7. A házak tervezési szempontjai

Mindkét egység háza FDM-eljárással, 3D nyomtatással készül. A nyomtatás mellett az szólt, hogy a házak darabszáma kicsi, a méretek a NYÁK-hoz igazíthatók, és a ház többször módosítható, amíg az illeszkedés nem megfelelő. Mindkét háznál közös szempont, hogy az antenna környezetében nem lehet fém, a csatlakozók (USB-C, OBD) terhelését a háznak kell felvennie, és a műanyagnak ki kell bírnia az utastér hőmérsékletét.

**Anyagválasztás.** Az utastér a napsütötte műszerfal környékén a becslések szerint elérheti a 80–85 °C-ot. A PETG üvegesedési hőmérséklete ≈ 80 °C körüli, az ABS-é és az ASA-é ≈ 100 °C feletti, ezért tartós járműbeli használatra az ABS vagy az ASA a megfelelő; a PETG a prototípusokhoz és mérsékelt hőmérséklethez elegendő. Az egységek hődisszipációja kicsi (az OBD-egységé átlagosan ≈ 0,4–0,5 W), külön szellőzés a hőtermelés miatt nem szükséges; a kijelzőegységnél az LDO terhelése (M1.5) miatt viszont szellőzőnyílások szükségesek lehetnek. A nyomtatás Bambu Lab P1S nyomtatóval, BambuStudio szeletelővel, alapbeállításokkal történt: 0,20 mm rétegvastagság, 10 %-os kitöltés, a programba épített fa-támaszték, mindkét háznál vasalt legfelső rétegekkel. [A szerző kiegészítése: a nyomtatott anyag és a további beállítások.]

**OBD-egység háza.** A ház vízszintesen kettéosztott. A NYÁK fészkekben ül, az USB-csatlakozó környékén illesztett támasztékok az USB-dugózás erőit a házra vezetik, nem a forrasztási pontokra. A két felet és a NYÁK-ot egyetlen M3-as csavar rögzíti, a fészkek és a NYÁK körvonala pedig megakadályozza a kártya elfordulását a csavar körül. A J1962 csatlakozó az egyik oldalon kilép a házból, mellette az USB-nyílás hozzáférhető marad a firmware-frissítéshez. Az illesztési hézag 0,2–0,3 mm a NYÁK körül, a falvastagság legalább 1,6–2 mm. Az antennazóna körül nincs fém (sem betét, sem csavar), a műanyag az antenna fölött vékony és egyenletes.

**Kijelzőegység háza.** A kártya kör alakú, három M2-es rögzítőfurattal (MH01–MH03), amelyek az elfordulásmentes rögzítést szolgálják. A hátoldali akkumulátor-zóna (40,5 × 30 mm) méretét a ház mélységénél figyelembe kell venni: a tervezett LP803040 cella 8 mm vastag, és a LiPo cellák használat közbeni duzzadására kb. 10 % tartalékot kell hagyni. A gombok (BOOT1, EN1) és az USB-C csatlakozó a házon kívülről elérhető, hogy a firmware-feltöltéshez és az újraindításhoz ne kelljen a házat szétszedni.

## M1.8. Kapcsolási rajzok és gyártási adatok

[A kapcsolási rajzok, a NYÁK-tervek (KiCad-projektek) és a gyártási fájlok (gerber, alkatrészlista) a repóban találhatók; ide a végleges változatban a két kapcsolási rajz oldalai és a hivatkozás kerül.]
