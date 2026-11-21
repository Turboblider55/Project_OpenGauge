# A GMI-nyilatkozat kitöltési útmutatója

Ez az útmutató a „Nyilatkozat a generatív mesterséges intelligencia eszközök használatáról" kitöltéséhez ad becslést. A nyilatkozat dokumentuma kitöltetlen marad: a számokat a hallgatónak kell megerősítenie, és ő írja alá.

## Mit ellenőriz a nyilatkozat

A sablon szerint meg kell adni, hogy használtam-e GMI-t, melyik eszközt, és minden feladattípusnál a **dolgozat melyik fejezeteiben és oldalain**, valamint a használat **becsült arányát az adott fejezeten belül (%)**. Az aláírással a hallgató kijelenti, hogy a dolgozat önálló munka, a GMI csak segédeszköz volt, és a használat részletei átláthatók.

## Őszinte alapadatok

- **Eszköz:** Claude (Anthropic), Claude Code alkalmazásban (asztali változat), a dolgozat és a firmware készítése során. Ha más eszközt is használtál (pl. ChatGPT, fordító, nyelvi ellenőrző a JLCPCB- és eladói levelekhez), azt is fel kell venni.
- **A git-történet szerint** a 67 commitból 39 viseli a Claude-trailert; a NYÁK-tervezési commitok (szeptember–október eleje, KiCad) és a mechanikai munka a hallgatóé. A firmware kb. 21 000 sora (132 fájl) a Claude Code ülésein készült a hallgató irányításával; az első firmware-commit (2026. október 24.) nem viseli a Claude-trailert; hogy az abban lévő kezdeti kód mennyire készült MI-segítséggel, azt te tudod megmondani.
- **A dolgozat szövege:** a v0.5 draft szövegének jelentős részét Claude fogalmazta meg a hallgató anyagaiból (kapcsolási rajzok, leírások, napló, hibák), a hallgató átnézte és javította. A végleges arányt az dönti el, mennyit írsz át saját szavaiddal.
- **A hardvertervezés (kapcsolás, NYÁK-elrendezés, ház) a hallgató munkája.** A hardver leírásában és hibalistájában az MI szerepe az átnézés, az ellenőrző számítások, a dokumentáció és a hibák rendszerezése volt; ez külön, pontosan leírandó.

## A táblázat soronként (becslés, a végleges fejezetszámok és oldalak a szöveg lezárása után írhatók be)

A százalék azt jelenti, hogy az adott fejezeten belül a GMI részesedése mekkora. Az alábbi értékek a **jelenlegi állapot őszinte becslései**; a végleges dolgozatra felül kell vizsgálni.

| Feladat | Érintett fejezetek | Becsült arány | Megjegyzés |
|---|---|---|---|
| kutatási ötletek generálása | 3. (szoftver) és 4. (tesztelés) | 30–40 % | Megoldási alternatívák, tesztötletek, hibahipotézisek (pl. a háttérvilágítás-PWM zaja). A szándékot, a döntést és a BLE-összefüggés felismerését a hallgató adta. |
| szövegalkotás | Bevezetés, 1., 2., 4., 5. fejezet, összefoglalás | 1. és 2. fejezet: 70–80 %, 3. fejezet: 80–90 % (a megírandó rész), 4. fejezet: 70 % | Ez a legnagyobb tétel. A százalék a hallgató átírásával csökken. Ha a végleges szöveg jelentős részét saját szavaiddal írod át, ennek megfelelően csökkentsd. |
| átfogalmazás | minden fejezet | 30–40 % | A hallgató jegyzeteinek, a napló és a commit-üzenetek tartalmának kifejtése szakdolgozati nyelvre. |
| adatgyűjtés | 1., 2. és 3. fejezet | 40–50 % | Adatlapi értékek (TPS54302, SN65HVD231, TP4056, ESP32 áramfelvétel), szabványok. **Minden adatot a hallgatónak az adatlapból ellenőriznie kell.** |
| adatelemzés | 4. fejezet | 40–50 % | A mérési naplók (ESP-NOW 2 óra, BLE-mérések) elemzése, a rajzolási idők és a memóriamérések összegzése; a méréseket a hallgató végezte. |
| források keresése | 1., 2. és 3. fejezet | 50–60 % | A javasolt források ellenőrzése a hallgató feladata: minden hivatkozásnál meg kell nézni, hogy létezik, és tartalmazza-e az állítást. |
| nyelvi és stilisztikai ellenőrzés | minden fejezet | 50 % | |
| hivatkozások generálása | szövegbeli [n] hivatkozások, Irodalomjegyzék | 60–70 % | A formázást és a sorszámozást Claude végezte. A hivatkozások tartalmi ellenőrzése a hallgató dolga. |
| kérdőívek/interjúk elemzése | – | nem releváns | Nem használtam. |
| ábrák, grafikonok, diagramok, infografikák készítése | 1. fejezet 1–4. ábra, 3. fejezet blokkvázlat és diagramok | a generált ábrákra 100 %, a fényképekre és a KiCad-, Blender-képekre 0 % | Ellenőrizendő: mely ábrákat készítette Claude (kód vagy vektorgrafika), melyeket te. A fényképek, a NYÁK-képek és a képernyőképek a hallgatóéi. |
| nyelvi fordítás | Idegen nyelvű összefoglalás; a forrásokból átvett angol tartalom | 90–100 % az összefoglalóra | |
| szoftver elkészítése | 3. fejezet; az egész firmware | 85–90 % a kódírásra | A firmware kb. 21 000 sora. A hallgató adta a követelményeket, a hardvert, a döntéseket, a teszteket és a hibakeresést (hardveres mérések), és minden változtatást ellenőrzött. Lásd az M5 mellékletet. |
| egyéb | 4. fejezet, M3, M4 | – | Hibakeresés (naplók, mérések értelmezése), PC-s tesztek írása, mérőszkriptek, a fejlesztési napló vezetése és a hardveres dokumentáció átnézése. |

## Mire kell figyelni

1. **A nyilatkozat kijelenti, hogy a dolgozat önálló munka, a GMI csak segédeszköz.** A kódírás és a szövegalkotás magas aránya mellett ez azt jelenti, hogy a hallgatónak a védésen minden részletet meg kell tudnia magyarázni, és az elemzések, értékelések a saját gondolatait kell hogy tükrözzék. Javaslat: a következtetéseket és az értékelő részeket saját szavaiddal írd át.
2. **Őszintén, de nem alulbecsülve.** Az MI-használat magas aránya nem hiba, ha átlátható és tudatos. Az M5 melléklet pontosan ezt mutatja: ki mit csinált, hogyan ellenőrizted, hol döntöttél te.
3. **Az M5 melléklet (MI-használat dokumentálása)** a nyilatkozat mellé kerül: a használat célja, az eszközök, a munkamegosztás táblázata (a hallgató: követelmények, hardvertervezés, ház, mérések, döntések, átnézés; Claude: kódírás, szövegvázlatok, dokumentáció, hibakeresési segítség), az ellenőrzési módszerek (PC-s tesztek, hardveres mérések, adatlap-ellenőrzés, szakirodalom-ellenőrzés) és a korlátok.
4. **Digitális kitöltés:** a nyilatkozat egyszerű Word-dokumentum (nincsenek kitölthető űrlapmezők). A táblázat beírható, az „aláhúzandó" részek aláhúzhatók, de az aláírás nyomtatást vagy elektronikus aláírást igényel. A Kar útmutatója nem mondja ki, hogy elfogadható-e a digitális aláírás; ezt a konzulenstől vagy az oktatási irodától kell megkérdezni.
5. **Végleges kitöltés:** a fejezetszámokat és oldalszámokat akkor kell beírni, amikor a dolgozat szövege és terjedelme lezárult, és a PDF oldalszámozása végleges.

## Amit nem tudok, és a hallgatónak kell megerősítenie

- Használtál-e más MI-eszközt (ChatGPT, Copilot, fordítók, nyelvi ellenőrzők), és mire.
- Az első firmware-commit (2026. október 24.) és a hardverleírások angol és magyar változatai (október 19–20.) mennyire készültek MI-segítséggel.
- Mely ábrák készültek MI-segítséggel.
- A hardvertervezés során volt-e MI-segítség (alkatrészválasztás, ellenőrző számítások, a kapcsolás átnézése), és mennyi.
