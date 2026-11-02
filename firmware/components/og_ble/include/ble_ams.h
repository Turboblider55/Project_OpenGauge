#pragma once
// Apple Media Service (AMS) kliens: az iPhone "Most játszik" adatainak olvasása és a lejátszás vezérlése BLE-n.
//
// Mit csinál: az eszköz csatlakoztatható hirdetést ad (az AMS-t kérő "solicitation" UUID-vel), az iPhone
//             csatlakozik és párosít; ezután az eszköz felfedezi az iPhone AMS-szolgáltatását, feliratkozik
//             a lejátszó- és a számadatokra (előadó, album, cím, hossz, állapot, eltelt idő, hangerő), és
//             lejátszásvezérlő parancsokat küldhet (lejátszás/szünet, következő, előző, hangerő).
// Hogyan:     az iPhone a GATT-kiszolgáló, az eszköz a kliens; az AMS csak titkosított (kötött) kapcsolaton
//             érhető el, ezért Just Works párosítás és tartós kötés (NVS) kell. Az adat lejátszó-függetlenül
//             a rendszer szintjén jön (Spotify, Apple Music, podcast, ...).
// Miért:      az iOS-es zeneadatok és -vezérlés (S-14) a műszerfal "Most játszik" képernyőjének alapja.
#include <cstdint>

namespace og {

enum class AmsCommand : uint8_t {
    Play = 0,
    Pause = 1,
    TogglePlayPause = 2,
    NextTrack = 3,
    PreviousTrack = 4,
    VolumeUp = 5,
    VolumeDown = 6,
};

struct AmsInfo {
    bool connected = false;  // van-e BLE-kapcsolat az iPhone-nal
    bool encrypted = false;  // titkosított (párosított) a kapcsolat
    bool ready = false;      // az AMS felderítve, a feliratkozások kész
    int playback = -1;       // -1 ismeretlen, 0 szünet, 1 lejátszás, 2 visszatekerés, 3 előretekerés
    float rate = 0;          // lejátszási sebesség (1,0 = normál)
    float elapsed_s = 0;     // az eltelt idő a legutóbbi frissítéskor
    float duration_s = 0;    // a szám hossza
    float volume = -1;       // 0..1; -1 ismeretlen
    char player[32] = {};    // a lejátszó neve (pl. "Spotify")
    char artist[64] = {};
    char album[64] = {};
    char title[64] = {};
    uint32_t updates = 0;    // vett frissítések száma (változás-jelző)
};

// Mit csinál: elindítja a NimBLE-t párosítással és kötéssel, és AMS-t kérő hirdetést ad a megadott néven.
// Fontos:     bontás után a hirdetést újraindítja; a ble_stop() után újra hívható.
bool ble_ams_start(const char* name);

// Mit csinál: pillanatfelvétel az iPhone-tól kapott adatokról (szálbiztos).
AmsInfo ble_ams_info();

// Mit csinál: lejátszásvezérlő parancsot küld az iPhone-nak. Hamis, ha nincs kész kapcsolat vagy a verem foglalt.
bool ble_ams_command(AmsCommand cmd);

// Mit csinál: törli a tárolt kötéseket (ha az iPhone-on "Eszköz elfelejtése" történt, és az újrapárosítás elakad).
void ble_ams_forget_bonds();

}  // namespace og
