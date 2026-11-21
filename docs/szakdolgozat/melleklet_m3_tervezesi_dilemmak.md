# M3. melléklet: Tervezési dilemmák és hibakeresési menetek

A melléklet a 3. fejezetben röviden leírt problémák menetét tartalmazza: mit figyeltünk meg, mit gondoltunk, mit próbáltunk ki, mi igazolódott, és mi lett a döntés. A teljes, időrendi anyag a fejlesztési naplóban található (Hibák, Változtatások és Tervezés lap); az azonosítók (H-n: hiba, V-n: változtatás) arra mutatnak.

## M3.1. A CAN-adóvevő típusa és az OBD-egység alvása (H-11)

**Helyzet.** Az OBD-egységnek a járműakkumulátorból kell működnie, ezért járó motor nélkül alacsony fogyasztású állapotban kell lennie, a gyújtás bekapcsolása után legfeljebb 3 s-on belül azonban működnie kell. A tervezett ébredési forrás a CAN-busz forgalma volt: az SN65HVD230 készenléti (standby) módjában a vevő aktív marad (kb. 370 µA), az RXD láb követi a buszt, így egy jelváltás felébreszthette volna a mikrovezérlőt.

**A hiba.** A kapcsolási rajzra és a megrendelésbe az SN65HVD231 került. Alvó (sleep) módjában, ha az RS láb a tápfeszültség 0,75-szörösénél magasabb, a vevő is kikapcsol (0,1 µA), így a busz forgalmát nem jelzi [28]. A két alkatrész lábkiosztása azonos, ezért a tévedés a kapcsolás átgondolásakor, a működési forgatókönyv végigvezetésekor derült ki, nem hardveres próbán.

**Mérlegelt megoldások.**

| Megoldás | Előny | Hátrány |
|---|---|---|
| Az IC kézi cseréje a kész lapon | az eredeti terv szerinti működés | kézi forrasztás a kész NYÁK-on (kockázat), nem skálázható |
| Külön gyújtásérzékelő vezeték | egyértelmű jel | a NYÁK-on nincs erre bemenet |
| Folyamatos ébrenlét | egyszerű | a járműakkumulátor lemerülését kockáztatja |
| Időzített figyelés (RS alacsony, rövid ablak) | a kész hardveren megvalósítható | ébredési késés, nem nulla átlagfogyasztás |

**Döntés.** Időzített figyelés: 10 s buszcsend után light sleep, 1 s-os ébresztési periódus, 60 ms-os figyelőablak az RS láb alacsonyra állításával. A periódus a legfeljebb 1 s-os késés és a fogyasztás közötti kompromisszum; az ablak hossza arra épül, hogy a jármű CAN-buszán a keretek ennél sűrűbben követik egymást.

**Eredmény.** 13 mérésben az első nyugtázott keretig 0,1–1,04 s telt el (átlag kb. 0,58 s). A megoldás a Rev. B-ben az IC cseréjével kiváltható.

**Tanulság.** A működési forgatókönyveket (ki ébreszt, mivel és mikor) az alkatrészválasztással egy időben, adatlapi összevetéssel kell ellenőrizni. Az azonos lábkiosztás és tok megnehezíti a tévedés észrevételét.

## M3.2. A belső RAM: BLE, LVGL és a PSRAM (H-34, H-37, H-38)

**Helyzet.** Az ESP32-S3 kijelzőn az LVGL, az ESP-NOW és a Bluetooth LE együtt fut. Műszerfal módban a szabad belső RAM kb. 58 kB (a legnagyobb összefüggő blokk 31 kB).

**1. fázis: a BLE nem indul (H-34).** A NimBLE host és a vezérlő a belső RAM-ból foglalt; indításkor a szabad RAM 0-ra fogyott, a host nem szinkronizált, és az ESP-NOW-adatfolyam is megszakadt. Következmény: az alvó kijelző BLE-re nem tudott felébredni, az ébredési lánc nem működött. Javítás: a NimBLE host a PSRAM-ba (`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`), egy kapcsolat és egy pár. A szabad belső RAM BLE indításakor 58 kB-ról 23 kB-ra csökkent.

**2. fázis: hiányzó rajzelemek (H-37).** A mutatós képernyők külső köríve és a piros sáv nem jelent meg, miközben a számok és az osztások igen, hibaüzenet nélkül. Ok: az LVGL fix, 64 kB-os belső kupaca elfogyott, a rajzolási feladatok memóriafoglalása csendben meghiúsult, az LVGL naplózása pedig ki volt kapcsolva. Ideiglenes javítás: a körívek valódi `lv_arc` objektumok, a kupac 72 kB.

**3. fázis: újraindulás (H-38).** A beállítások képernyő lusta felépítése után 60 kB-os kupaccal a beállítások megnyitásakor a kijelző újraindult (StoreProhibited az `lv_draw_sw_transform` függvényben: a forgatott mutató ideiglenes rajzpufferének foglalása NULL-t adott). A lusta felépítés nem segített, mert a kupac csúcsát nem a nyugalmi használat, hanem a beállítások megnyitása adta (27 kB + 24 kB + a rajzpuffer).

**Döntés.** Az LVGL saját memóriafoglalója a PSRAM-ot használja (`lv_mem_psram.c`). Eredmény: a szabad belső RAM BLE mellett 9 kB-ról kb. 80 kB-ra nőtt, a minimum 4,4 kB-ról 77 kB-ra; a képfrissítési sebesség nem változott (48–50 képkocka/s). Az ellenérv a PSRAM lassabb elérése volt; a rajzolási idők mérése ezt nem igazolta.

**Tanulság.** Az első két fázisban részmegoldások születtek (a BLE-é, majd a kupac növelése), amelyek a következő korlátot csak elhalasztották. A memóriakorlátnál a profilozás (belső RAM, PSRAM, LVGL-kupac és a feladatok vermei az indulás lépései szerint) az első lépés kell legyen. Külön tanulság, hogy a szabad memória elfogyása itt nem hibaüzenetként, hanem hiányzó vagy hibás megjelenítésként jelentkezett.

## M3.3. Hallható zaj a kijelző NYÁK-on (H-51, H-52)

**Megfigyelés.** Töltés közben halk, vibráló zúgás hallatszott a kijelző NYÁK-ról, rosszabb USB-kábellel erősebben.

**Első hipotézis és kísérlet.** A zaj forrása a háttérvilágítás 5 kHz-es PWM-je lehet: a hallható tartományban lévő áramlöketek a kerámia kondenzátorok mikrofonhatásán (piezoelektromos hatás) keresztül hangot keltenek. A PWM frekvenciájának 25 kHz-re emelésével a zúgás megszűnt (V-66). A PPTC-biztosíték (F1) nem okozta, mert nem szabályoz áramot.

**Megmaradt zaj és kísérletek (H-52).** A zúgás megszűnte után kb. 10 Hz-es kattogás maradt, töltés közben és akkumulátoros üzemben is. A kísérletek:

1. A háttérvilágítás minimuma és maximuma között nincs hangbeli különbség: a PWM nem forrás.
2. Akkumulátor nélkül, USB-ről a TP4056 piros (CHRG) LED-je kb. 10 Hz-en villog, és hasonló kattogás hallható. Ez a TP4056 ismert, akkumulátor nélküli viselkedése (a töltő a terhelés és a kimeneti kondenzátor miatt ismétlődően töltés és befejezés között vált); nem hiba, de zajforrás.
3. Akkumulátorral csak a zöld LED világít, kb. másodpercenként egy halk kattanás hallható.
4. Csak akkumulátorról nagyon halk, éles sípolás és alig hallható kattogás marad. A zene (BLE) képernyő kikapcsolásakor a zaj megszűnik, tehát a zaj a BLE-tevékenység 10 Hz-es áramlöketeihez kötődik.
5. Az acélcsavarhúzóval megérintett C7 kondenzátor (10 µF, 1206, a rendszersínen) esetén a zaj élesebb és hangosabb, a többinél nem: a C7 kijelölhető hangforrásnak, de a gerjesztést ez nem azonosítja.

**Értékelés.** Brownout vagy újraindulás nincs, tehát a tápsín nem esik be jelentősen, és nem bizonyított, hogy a rádió működését érinti. A zárt ház a hangot erősíti. A zaj funkcionális hatás nélküli, ezért nem javítottuk.

**Rev. B-re.** A bulk kapacitás polimer vagy tantál kondenzátorra cserélése vagy kiegészítése, az X5R/X7R kerámiák előfeszítés okozta kapacitáscsökkenésének figyelembevétele, a TP4056 CHRG/STDBY jelének bevezetése a mikrovezérlőre.

**Tanulság.** Az első hipotézis csak a jelenség egy részét magyarázta; a megoldás részleges lett. A második kísérletsorozat kimutatta, hogy a megmaradt zaj nem a PWM-hez, hanem a BLE-hez és a töltőhöz kötődik, és hogy a „megoldva” megállapítást érdemes az ellenőrző kísérletek után, nem a javítás után kimondani.

## M3.4. A fokozatbecslés módszere

**Helyzet.** Az OBD-II-ben nincs egységes fokozat-PID, a váltóvezérlő adata csak gyártóspecifikusan, járműfüggő módon érhető el. A fokozatot a fordulatszám és a sebesség k arányából lehet becsülni.

**Mérlegelt módszerek.** (1) Gyártóspecifikus lekérdezés: járműfüggő, a hatókörön kívül. (2) Képlet a főhajtás, a kerékkerület és az áttételek megadásával: pontos, de a felhasználónak ismernie kell az adatokat. (3) Tanulás vezetés közben: járműfüggetlen, de a pontosság a vezetési mintától függ.

**Döntés.** Mindkét módszer megvalósult: ha az adatokat megadják, a képlet működik, különben a készülék tanul. A tanulás csak stabil szakaszokból dolgozik, a fürtök száma a fokozatszámnál nem lehet nagyobb, és a fokozatot csak akkor adja meg, ha mind ismert.

**Hibák menet közben.**

- *Az 1. fokozat nem tanulódott meg.* Kis sebességnél a sebesség egész km/h-ra kerekített (PID 0D), így 20 km/h körül a k ±2,5 %-ot ugrált, és a stabilitási feltétel nem teljesült. A tűrés sebességfüggő lett (4 % és 1/sebesség), a tanulás alsó határa 12 km/h. Az ECU-szimulátoron ezután az öt fokozat kb. 2 perc alatt megtanulódott.
- *Az „N” jelzés nem jelent meg.* Az ECU-szimulátor régi firmware-t futtatott (nem volt benne gurulás üresben), és állás közben a szabály a 15 km/h-s alsó határon elbukott. Az új szabály járó motornál, 0 km/h-nál kb. 1 s után jelez N-t.

**Igazolás és korlát.** A módszert csak az ECU-szimulátoron igazoltuk; valós járművön, automata és fokozatmentes váltónál nem tesztelt. Az egész számra kerekített sebesség (PID 0D) alapvető korlát kis sebességnél.

**Tanulság.** A szimulátor és a valós jármű eltérhet: az N-jelzés hibájának egyik oka a szimulátor elavult firmware-e volt, ezért a teszteszköz verzióját is nyilván kell tartani.
