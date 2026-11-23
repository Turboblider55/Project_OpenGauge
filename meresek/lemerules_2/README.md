# Lemerülési teszt, 2. futás (kijelzőegység, akkumulátoros üzem)

## Cél
Az 1. futás (`../lemerules_1`) megismétlése teljesen feltöltött cellával, hogy az üzemidő a teljes tartományra megmérhető legyen. A mérés módja azonos: a kijelző az életjelben küldi a szűrt akkumulátorfeszültséget, a C3 30 s-onként naplózza, a naplót a `serial_logger.ps1` írja.

## Feltételek
- Indulás: a cella teljesen feltöltve (a TP4056 zöld LED-jének felvillanásakor az USB lehúzva), 4,17 V a naplóban (kb. 96 %).
- Firmware: 2026-11-17 22:26:13 fordítás; CPU 240 MHz; LCD pclk 40 MHz; zene/BLE be; elalvási idő 20 s (gyári), de az ECU-szimulátor folyamatosan adott adatot (a C3 ESP-NOW-n küldte a kijelzőnek), az életjel folyamatos, ezért nincs alvás; debug mód be (FPS-kijelzés).
- Háttérvilágítás: beállított 50 %; 3,5 V alatt a szoftver 40 %-ra korlátozza (a naplóban „fenyero 40% (beallitott 50%)”, a korlát 17:57-től működik).

## Eredmények (a `c3_obd.txt` alapján)
- A napló 2026-11-20 12:59:49-től 19:58:55-ig tart, 834 akkumulátorsor, lyuk nélkül: **kb. 7 óra** folyamatos üzem.
- Feszültség az idő függvényében: 4,17 V (0 h), 3,87 V (1 h), 3,72 V (2 h), 3,60 V (3 h), 3,54 V (4 h), 3,50 V (5 h), 3,40 V (6 h), 2,91 V (7 h).
- Küszöbátlépések az indulástól: 4,0 V 0,29 h; 3,8 V 1,48 h; 3,7 V 2,18 h; 3,6 V 3,01 h; 3,5 V 4,96 h; 3,4 V 5,99 h; 3,3 V 6,64 h; 3,2 V 6,81 h; 3,1 V 6,90 h; 3,0 V 6,96 h.
- A feszültség 3,4 V alatt meredeken esik (3,4 V-ról 3,0 V-ra kb. 1 óra alatt, a legvégén kb. 4 perc alatt 3,1 V-ról 3,0 V-ra).
- **Egy újraindulás** 19:57:25-kor, 2,96 V-nál (ugyanaz a küszöb, mint az 1. futásban: 2,96 V). Az újraindulás után a napló még két sort tartalmaz (2,93 V és 2,91 V, üzemidő 63 s és 93 s), utána véget ér.

## Értelmezés és korlátok
- A két futás összevethető a 3,6 V-os ponttól: az 1. futásban 3,62 V-ról 2,96 V-ig 4,6 h (40 % háttérvilágítás), a 2.-ban 3,60 V-ról (3,0 h) 2,96 V-ig kb. 4,0 h (50 %, majd 40 %). A különbség a háttérvilágítás és a debug mód hatásán belül van; külön mérés nincs.
- Az átlagáram a papír szerinti 1000 mAh kapacitással csak becsülhető (a 4,17 V és 3,0 V közötti teljes kisütés kb. 7 óra, tehát az átlagáram a nagyságrendje 140 mA); a tényleges kapacitás nem mért.
- A napló vége (19:58:55, a kijelző 93 s-os üzemidejénél) nem a naplózó saját lezárása: a naplózó 480 percre volt beállítva, és nincs „Naplózás vége” sor; a fájl utolsó módosítási ideje megegyezik az utolsó sor időbélyegével. A szűrt naplóba csak a kijelző életjelére épülő sorok kerülnek, ezért a napló vége az életjel megszűnését mutatja; hogy a naplózó ezután futott-e még, és a kijelző töltés után újraindult-e, a naplóból nem derül ki (a felhasználó szerint a töltés után sem került sor a fájlba).
