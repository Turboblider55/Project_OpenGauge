#pragma once
// OBD-II kliens: egy kérés-válasz váltás a P2/P2* időzítések betartásával.
//
// Mit csinál: kérést küld (funkcionális vagy egy kiválasztott ECU fizikai címére), megvárja a
//             hozzá tartozó pozitív választ, kezeli a negatív és a "response pending" választ.
// Hogyan:     az ISO-TP rétegre épül; küldés előtt kiüríti az elavult kereteket, fogadáskor
//             kiszűri a más ECU-tól vagy más kérésre érkező válaszokat.
// Miért:      a hívó (PidScheduler, protokoll-felismerés) így egyetlen hívással kap értelmes
//             eredményt vagy pontos hibakódot.
#include <cstddef>
#include <cstdint>

#include "isotp.h"
#include "obd_services.h"
#include "og_err.h"

namespace og {

struct ObdClientConfig {
    // A válasz első keretének határideje. A szabvány szerinti P2CAN legfeljebb 50 ms (J1979-2
    // 12. táblázat); a kliens ennél tágabb, 100 ms-os határidőt használ a jitter miatt.
    uint32_t p2_ms = 100;
    // "Response pending" (NRC 0x78) után ennyit vár (P2*CAN legfeljebb 5000 ms).
    uint32_t p2_star_ms = 5000;
};

class ObdClient {
public:
    ObdClient(IsoTp& tp, const ObdClientConfig& cfg = ObdClientConfig()) : tp_(tp), cfg_(cfg) {}

    // Mit csinál: beállítja, melyik ECU-nak szóljanak a kérések. 0 = funkcionális (mindenki).
    // Hogyan:     fizikai kérés-azonosító (pl. 0x7E0), és csak ettől az ECU-tól fogad választ.
    // Miért:      a felderítés után egyetlen ECU-ra szűkítve nincs többszörös válasz.
    void set_target(uint32_t request_id) { target_ = request_id; }
    uint32_t target() const { return target_; }

    // Mit csinál: nyers kérés-válasz. A választ a resp pufferbe másolja (a SID-vel együtt).
    // Hogyan:     lásd a fájl fejlécét; a negatív választ NegativeResponse-ként adja vissza,
    //             a kódot last_nrc() adja meg.
    // Miért:      az összes többi lekérdezés erre épül.
    Err request(const uint8_t* req, size_t req_len, uint8_t* resp, size_t cap, size_t& resp_len);

    // Mode 01: egy PID adatbájtjai (a 41 és a PID nélkül). data legalább 6 bájt legyen.
    Err read_pid(uint8_t pid, uint8_t* data, size_t cap, size_t& len);

    // Támogatott-PID bitmap lekérdezése (base = 0x00, 0x20, 0x40).
    Err read_supported(uint8_t base, uint32_t& bitmap);

    // Mode 09 PID 02: a VIN (17 karakter + lezáró 0). Többkeretes válasz.
    Err read_vin(char vin[18]);

    // Mode 03: tárolt hibakódok darabszáma (csak olvasás; törlés nem része a projektnek).
    Err read_dtc_count(uint8_t& count);

    uint8_t last_nrc() const { return last_nrc_; }
    uint32_t last_source_id() const { return last_source_; }
    uint32_t now_ms() { return tp_.now_ms(); }

private:
    bool response_matches(const uint8_t* req, size_t req_len, const uint8_t* resp, size_t resp_len) const;

    IsoTp& tp_;
    ObdClientConfig cfg_;
    uint32_t target_ = 0;
    uint8_t last_nrc_ = 0;
    uint32_t last_source_ = 0;
};

}  // namespace og
