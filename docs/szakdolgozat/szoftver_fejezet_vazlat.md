# A szoftveres fejezet vázlata

Munkaanyag a szakdolgozat 3. fejezetéhez („Szoftvertervezés és -kialakítás"). Célterjedelem: 10–11 oldal a törzsszövegben. Minden pont mellett szerepel a tervezett terjedelem, a forrás a fejlesztési naplóban (H = hiba, V = változtatás, S = szoftver-feladat, Q = nyitott kérdés) és a melléklet, ahová a részletek kerülnek.

## Vezérfonal

A fejezet gerince: **a hardver hibái és hiányosságai szoftveresen hogyan voltak kezelhetők, és mi lett ennek az ára.** A szoftverfejezet így nem funkciólista, hanem döntések és kompromisszumok története, ahogy az útmutató is kéri (érvek, ellenérvek, saját döntés). A funkciók ezeknek a történeteknek a keretében kerülnek elő.

Az útmutató szerint külön ki kell emelni az önálló munkát és a kudarcokat, de a kudarc ne legyen a fő vonal. Ezért a hibák a döntési történetekbe ágyazva szerepelnek, a teljes lista a mellékletben (M3).

## 3.1 Követelmények, környezet és hatókör (0,75 oldal)

- A szoftver feladata a két egységen: OBD-II adatok kiolvasása és továbbítása (OBD-egység), fogadás, megjelenítés, alvás és ébredés (kijelzőegység), valamint egy PC-s és egy ESP32-S2-es tesztkörnyezet.
- Környezet: ESP-IDF 5.5.3, C és C++, FreeRTOS, VS Code; a fordítás az ESP-IDF terminálból (V-09, V-38).
- Hatókör: csak olvasás; a hibakódtörlés és az OBDonUDS (J1979-2) szándékosan kimarad, utóbbit az eszköz csak felismeri és jelzi (V-27).
- **Döntés:** ESP-IDF az Arduino-kerettel szemben (alvás, koegzisztencia, memóriakezelés finom vezérlése). Érv és ellenérv egy-egy mondatban.

## 3.2 Architektúra és tesztelhetőség (1,25 oldal)

- Komponensek: `og_protocol`, `og_obd`, `og_common`, `og_twai`, `og_espnow`, `og_ble`, `og_led`, `disp_lcd`, `disp_model`, `disp_smooth`; blokkvázlat (ábra).
- Alapelv: a tiszta logika hardverfüggetlen (`ICanBus`; PC-n `VirtualBus`, hardveren `TwaiBus`), ezért az ISO-TP, az OBD-kliens, a PID-ütemező és a fokozatbecslés PC-n tesztelhető (13 tesztcsomag, MSVC és GCC; H-22, V-34).
- **Döntés:** a hardvertől független logika külön komponensekben, az ára: több interfész és egy külön tesztkörnyezet; a haszna: a hibák nagy része a hardver előtt kiderül.
- Futtatási modell: a kapcsolatfeladat a 0., az LVGL az 1. magon (V-39).
- Forrás a naplóban: V-26, V-33, V-35, V-39.

## 3.3 Az OBD-egység szoftvere (2,5 oldal)

- CAN-meghajtó, protokoll-felismerés (500 és 250 kbit/s, 11 és 29 bit, listen-only hallgatás, majd `01 00` kérés), ISO-TP többkeretes üzenetekhez (VIN), a P2 és P2* időzítés és a „response pending" kezelés.
- PID-ütemező: késés és periódus szerinti rangsor, busz-kihasználtság számítása (a számítás és a PID-táblázat a mellékletben, M2).
- **A fejezet egyik központi története: az SN65HVD230 helyett SN65HVD231 (H-11).** A 231 alvó módjában a vevő is kikapcsol, ezért az adóvevő nem tudja jelezni a busz forgalmát, amivel a mikrovezérlő felébredhetne. Következmények és megoldás:
  - nincs busz-ébresztés; helyette light sleep és időzített ébredés, rövid listen-only ablakban a CAN-RX GPIO-ból ébresztés, forgalom hiányában visszaalvás (V-12, S-12);
  - az ára: a gyújtás-felismerés késése az ébredési periódus függvénye; a 3 másodperces cél teljesül (Q-27);
  - az alternatíva (az IC kézi cseréje) a kész NYÁK-on nehézkes, ezért a megoldás szoftveres; a Rev. B-ben az IC cseréjével tisztán javítható;
  - tanulság: az alkatrész-kiválasztási hibát a tervezés ellenőrzése során vettük észre, nem a működő hardveren; ezt a hibák őszinte bemutatásánál érdemes leírni.
- Mellékes, de tanulságos: a C3 USB-Serial/JTAG naplója light sleep után elnémul (H-33), ezért az ébredést a szimulátor oldaláról mértük.
- Forrás: H-11, H-32, H-33, H-21; V-12, V-35; S-02, S-03, S-12, S-18.

## 3.4 Kommunikáció és rádió (1,5 oldal)

- Üzenetformátum: verzió, sorszám, adatmező; kétirányú életjel (1 Hz) (S-01, S-20); a teljes keretleírás a mellékletben (M2).
- ESP-NOW szórásos adás 10 Hz-en, a LinkStats csomagveszteség-számítással; az adó újraindulásának felismerése és a 60 s-os gördülő arány (H-36, V-45). 2 órás asztali mérés: 0,11 % csomagveszteség (V-41).
- **Döntés:** ESP-NOW és BLE együtt, ellenére, hogy az ESP-IDF koegzisztencia-táblázata szerint az ESP-NOW vétel BLE mellett korlátozott (H-16). Hogyan döntöttünk: korai teszt, tartalék terv (telemetria BLE-n), majd tartósmérés (90 perces BLE + ESP-NOW, V-45; az eredményt a naplóból és a mérési fájlokból kell kiolvasni a szövegírás előtt). Ez a fejezet másik fontos döntési története.
- **Elfogadott kockázat:** az ESP-NOW forgalom titkosítatlan és párosítás nélküli (H-48, Q-51); a PMK/LMK-titkosítás a továbbfejlesztés része (S-34).

## 3.5 Energiagazdálkodás és ébredés (1,5 oldal)

- Állapotgép (`PowerFsm`): OBD-oldalon light sleep és CAN-ablakos ébredés; kijelző-oldalon light sleep, BLE-szkennelés kis kitöltéssel (30 ms 1,5 s-onként), BLE-hirdetés az OBD-egységről (V-13, S-13).
- **Döntés: light sleep a modem sleep helyett a kijelzőn** (H-17): modem sleepben 22–33 mA, light sleepben 240 µA + a PSRAM; az ára az ébredési késés és a BLE ablak.
- **Hardverhiány szoftveres kezelése:** nincs külső 32,768 kHz-es kristály (H-15), ezért a BLE alacsony fogyasztású órája vagy a fő XTAL, vagy a belső RC lehet; a következmény a szkennelési ablak és az ébredési periódus kompromisszuma. Nincs CHRG-bekötés a mikrovezérlőre (H-31), ezért a töltést a feszültség 2 perces emelkedéséből becsüljük.
- Hibák és tanulságok: hideg indulásnál senki nem hirdette a BLE-ébresztőt (H-43, megoldva); az alvás fordításkori kapcsolója alapból ki volt kapcsolva, és a beállított idő nem érvényesült (H-42); az ébredés logóval, koppintással átugorható (V-65).
- BLE médiavezérlés iOS-en (Apple Media Service): kapcsolatfelügyelő, párosítás; az elakadó csatlakozások kb. 2–3/7–8 esetben (H-35, megoldva a kapcsolat-nyilvántartás javításával).
- Nyitott: a hidegindulás és az alvóáram mérése autóban (Q-07, Q-50).

## 3.6 A kijelző és a felhasználói felület (2,5 oldal)

- Meghajtó: ST77916 QSPI, a Waveshare saját init-táblája (H-19; az alapértelmezett tábla hamis színeket adott), a megcserélt reset-lábak (H-27), a visszaolvasás sikertelensége (H-20: valószínű szintillesztő, nem igazolt, elfogadott kockázat).
- Érintés: CST816, a reset utáni alvás (H-25), időzített lekérdezés a megszakításos mód helyett (H-26), a port hibakezelése (H-28).
- LVGL 9.6 és esp_lvgl_port: részleges frissítés 2 × 40 soros DMA-pufferrel; a 180°-os forgatás (H-45: az LVGL-port felülírta a panel tükrözését, ezért a konfigurációban kell megadni).
- **A memória története (egy rövid, jó példa a döntésre):** belső RAM fogyás a BLE indításakor (H-34), az LVGL-kupac elfogyása (H-37, H-38), majd az LVGL memóriájának áthelyezése a PSRAM-ba (V-54): a szabad belső RAM BLE mellett 9 kB-ról kb. 80 kB-ra nőtt. Érv-ellenérv: PSRAM lassabb, de a rajzolási idők mérése (V-50) szerint elfér.
- Felület: mutatós műszerek (előre rajzolt skála, forgatott élsimított tű), kettős mérő, fokozatközpontú képernyő, beállítások három képernyőn (V-51…V-60); adatsimítás (`disp_smooth`).
- Fokozatbecslés: a fordulat/sebesség arány, tanulás vagy képlet; a sebesség egész km/h-ra kerekítése miatti stabilitási feltétel (H-40) és az „N" jelzés (H-41). Valós autón még nem igazolt (Q-46, Q-47): ezt őszintén jelezni kell.
- A háttérvilágítás PWM-je: 5 kHz-en hallható zúgás töltés közben, 25 kHz-re emelve megszűnt (H-51, V-66).

## 3.7 Hardverhibák és -hiányosságok szoftveres kezelése (1 oldal, összefoglaló táblázat)

Ez a rész a fejezet összegző táblázata (a hallgató kérésére a legérdekesebb rész). Oszlopok: hiba vagy hiányosság | következmény | szoftveres megoldás | ára, korlátja.

| Hiba vagy hiányosság | Szoftveres megoldás | Ára, korlátja |
|---|---|---|
| SN65HVD231 a 230 helyett (H-11) | light sleep, időzített ébredés, RX-GPIO | késés; nincs valódi busz-ébresztés |
| Nincs 32,768 kHz-es kristály (H-15) | rövid szkennelési ablak, hosszabb periódus | fogyasztás és ébredési késés |
| Nincs CHRG a mikrovezérlőn (H-31) | töltés becslése a feszültségből | pontatlan; Rev. B-ben GPIO |
| Q1 és kapcsolódó hibák (H-01…H-05) | akkumulátoros tesztek, mérés | elfogadott kockázat; kézi utómunka nem készült |
| A modul reset-lábai fel vannak cserélve (H-27) | `board_pins.h` hardverváltozatonként | nincs |
| A szintillesztő miatt nincs visszaolvasás (H-20) | a diagnosztika kikerült a kódból | a regiszterek nem ellenőrizhetők |
| Rev. A kijelző-NYÁK 8 MB flash (H-29) | partíciótábla, 3 MB alkalmazás | nincs OTA (S-35) |
| Hallható zaj a tápsínen (H-51, H-52) | PWM 25 kHz; a BLE 10 Hz-es kattogása nyitott | Rev. B: bulk kondenzátor, tápszűrés |

(A részletes hibalista az M3 mellékletben.)

## 3.8 A szoftver értékelése: futási idő és helyigény (0,5 oldal)

- Kódméret: kb. 21 000 sor (132 fájl, a külső komponensek nélkül), az alkalmazás kb. 1,3 MB a 3 MB-os partícióban; szabad belső RAM kb. 80 kB BLE mellett; rajzolási idők és képfrissítési sebesség (LVGL-profilozó); a PID-ciklus kihasználtsága.
- Az útmutató szerint az elemzés ne csak a működést, hanem a futási időt és a helyigényt is mutassa. Az adatok a mérésekből (4. fejezet) jönnek, itt csak összegezzük.

## Terjedelem

| Pont | Oldal |
|---|---|
| 3.1 | 0,75 |
| 3.2 | 1,25 |
| 3.3 | 2,5 |
| 3.4 | 1,5 |
| 3.5 | 1,5 |
| 3.6 | 2,5 |
| 3.7 | 1 |
| 3.8 | 0,5 |
| Összesen | 11,5 (cél 10–11: a 3.3 és 3.6 tömörítendő) |

## Mi kerül a mellékletbe

- M2: az üzenetformátum, a PID-terv és a számítások, a forráskód-részletek, a komponens-szerkezet fájlonként.
- M3: a fejlesztési napló kivonata (H-01…H-52, a megoldott és nyitott hibák).
- M4: a mérési jegyzőkönyvek (ESP-NOW 2 óra, BLE + ESP-NOW 90 perc, rajzolási idők, memória).

## Ütközések, kérdéses lépések (a fejezet tartalmának nyersanyaga)

- **Tesztkörnyezet és a végleges hardver eltérése:** ESP32-S2 az OBD helyett (nincs BLE, H-18), N16R8 devkit az N8R8 modul helyett (H-29), CAN-modul SN65HVD230-cal, nem a lapon lévő 231-gyel.
- **Amit a szimulátor nem mutat meg:** a fokozatbecslés valós autón, a CAN-forgalom és az ébredés valós autóban, a hidegindulási áramfelvétel.
- **Amit nem oldottunk meg:** az ESP-NOW titkosítása, OTA, CI, a BLE kattogás (H-52).
- **Döntéseket, amelyeket a szakdolgozatban érv-ellenérvvel kell leírni:** ESP-IDF vs Arduino; ESP-NOW és BLE együtt; light sleep vs modem sleep; időzített lekérdezés vs megszakítás az érintőnél; LVGL memória belső RAM vs PSRAM; előre rajzolt skála vs élő rajzolás; fokozat tanulása vs megadott áttételek; PC-s tesztelhetőség vs közvetlen hardverfejlesztés.

## Kérdések, amiket a szöveg megírása előtt tisztázni kell

1. Lesz-e valós autós mérés (fokozatbecslés, ébredés, alvóáram) a leadás előtt? Ha nem, a dolgozat „nem teljesen igazolt" szakaszban írja le.
2. Melyik képernyőképek és fényképek állnak rendelkezésre (kijelző a műszerfal-képernyőkkel, beállítások, fokozatképernyő, mérések)?
3. A tesztelés fejezetben a mérési eredmények fájlja (`meresek/`) a melléklet alapja, rendben van-e így?
4. Szükség van-e a szoftverfejezetben UML vagy blokkvázlat? A javaslat: egy blokkvázlat (komponensek) és egy állapotdiagram (energiagazdálkodás).
