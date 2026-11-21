# Lemerülési teszt (kijelzőegység, akkumulátoros üzem)

## Cél
A kijelzőegység üzemidejének és az akkumulátorfeszültség alakulásának megfigyelése USB nélkül, valamint a lemerülés közeli viselkedés (újraindulás) vizsgálata. Az S3 az életjelben elküldi a szűrt akkumulátorfeszültséget a C3-nak, a C3 30 s-onként naplózza (V-68); a naplót a `serial_logger.ps1` írja, `-Include` szűréssel.

## Fájlok
- `c3_obd.txt`: a C3 szűrt naplója (akkumulátorsorok, a kijelző beállításai). Két futás van benne: 2026-11-17 22:43 és 2026-11-18 17:21 kezdettel (utóbbi elején a COM8 még nem volt csatlakoztatva, ezért sok "Port nem nyitható" sor van).
- `lemerules_gorbe.png`: a feszültség az idő függvényében az érvényes szakaszra.

## Feltételek (a kijelző saját jelentése szerint)
Változat: 2026-11-17 22:26:13 fordítás; CPU 240 MHz; LCD pclk 40 MHz; háttérvilágítás 40 %; elalvási idő 20 s (gyári); lapozás kézi; debug ki. A zene/BLE funkció az első futásban ki, a másodikban be volt kapcsolva (a beállítás megváltoztatása újraindította a kijelzőt, 17:28:47). Az ECU-szimulátor nem adott adatot, az alvás a naplósorok alapján nem következett be (a kijelző életjele folyamatosan érkezett).

## Eredmények
- 1. futás (2026-11-17): rövid, kb. 45 perc; a feszültség 4,18 V-ról 3,99 V-ra esett (az első percekben a töltés utáni relaxáció).
- 2. futás (2026-11-18): a kijelző 17:28:58-tól 22:04-ig, **kb. 4 óra 35 perc**-ig ment folyamatosan (3,62 V-ról).
  - 3,62 V (0 h), 3,57 V (1 h), 3,53 V (2 h), 3,47 V (3 h), 3,44 V (4 h).
  - 3,5 V alatt (kb. 2,6 h után) a háttérvilágítás-korlát (40 %) nem változtatott semmin, mert az alap fényerő is 40 % volt.
  - A görbe 3,4 V alatt hirtelen meredekebb: 3,35 V-ról 2,96 V-ra kb. 30 perc alatt.
  - **Újraindulás:** 22:04:03-kor (2,96 V), újra 22:04:31-kor (2,93 V), azután a kijelző már nem indult újra (az életjel megszűnt 22:05-kor).
- 21:28:56-kor egy mintán át megemelt érték látszik (3,44 V a 3,35 V helyett, RSSI -19 dBm): ekkor dugta be a felhasználó az USB-t (kézi próba, lásd lent). A kijelző üzemidő-számlálója ezután is folytatódik (14409 s, 14439 s, 14470 s, …), tehát újraindulás nem történt.

## Értelmezés és korlátok
- Az akkumulátor kapacitása nincs rögzítve, ezért az átlagáram nem számolható. Az üzemidő (4,6 h) csak a leírt feltételekre (40 %, BLE be, nincs alvás) érvényes.
- A feszültséget az ADC a 100 k / 100 k-os osztón át méri terhelés alatt; a kivett kapacitás és a feszültség kapcsolata nem ismert.
- A cella 3,0 V alá merült (2,93 V a naplóban). A cellán a felhasználó szerint van védőáramkör, küszöbe nem ismert; a cella szemrevételezéskor nem deformálódott, a kapacitás romlása ezért nem valószínű, de nem mért. A gyári (papír szerinti) kapacitás 1000 mAh.
- A kijelző szoftvere nem kapcsol ki egy alsó feszültségküszöbnél (csak a háttérvilágítást korlátozza 3,5 V alatt).
- Az alvó mód üzemideje nem mérhető ezzel az elrendezéssel (alvás közben nincs életjel).

## A tápút-átkapcsolás próbája (kézi próba a mérés közben)
A kijelző alacsony töltöttséget jelzett (3,35 V, a töltésjelző szerint 0 %), ekkor (21:28:56) a felhasználó USB-t dugott a kijelzőegységre, majd kihúzta, és figyelte, hogy újraindul-e a kijelző. **Nem indult újra**: az üzemidő-számláló folytatódik, és a feszültség az USB kihúzása után néhány percen belül visszaesik 3,35 V-ra. A dolgozat számítása (2.4.2. pont) 3,55 V alatt újraindulást jósolt az AO3401A testdiódás átkapcsolásakor, tehát a becslés konzervatív. A tápsín feszültségét oszcilloszkóp híján nem lehetett megmérni, ezért az ok (kisebb diódaesés vagy alacsonyabb brown-out küszöb; a firmware a legalacsonyabb, 7-es szintet használja) nem azonosítható.

## Az újraindulások a lemerülés végén
A naplóban látható kétszeri újraindulás (2,96 V és 2,93 V) USB nélkül, akkumulátoros üzemben történt, és utána a kijelző már nem indult újra. Hogy miért: nem ismert. Hipotézisek: (1) a lemerült cella belső ellenállása miatt az indulási áram alatt a sín beesik (brownout); (2) a cella védőáramköre alsó feszültségküszöbnél lekapcsolt. A két hipotézis a rendelkezésre álló mérésekből nem választható szét (a tápsín és a cella kapocsfeszültsége terhelés alatt nem volt mérhető).
