#pragma once
// Ütemező a ciklikus PID-lekérdezésekhez.
//
// Mit csinál: minden PID-hez külön frissítési periódust tart nyilván, és mindig azt a PID-et
//             kérdezi le, amelyik a periódusához képest a legjobban késik.
// Hogyan:     step() kiválasztja a legrégebben esedékes PID-et, lekérdezi az ObdClient-tel, és
//             eltárolja a dekódolt értéket. A "késés / periódus" arány szerinti sorrend miatt
//             a gyors PID-ek (fordulatszám) túlterheléskor sem éheztetik ki a lassúakat, és
//             fordítva.
// Miért:      a busz nem a szűk keresztmetszet, hanem az ECU válaszideje (kb. 40 ms/kérés),
//             ezért a kérések számát kell okosan elosztani a PID-ek között.
//
// Hiányzó kérés esetén a hiba nem állítja meg az ütemezőt: a PID a periódusa szerint újra
// sorra kerül, a hibaszámláló és az "életkor" jelzi a kliensnek, hogy az érték elavult.
#include <cstddef>
#include <cstdint>

#include "obd_client.h"
#include "pid_table.h"

namespace og {

class PidScheduler {
public:
    static constexpr size_t kMaxEntries = 12;

    // Mit csinál: új PID-et vesz fel adott periódussal (ms). Hamis, ha tele van / 0 a periódus.
    // Hogyan:     az első lekérdezés azonnal esedékes (now).
    bool add(uint8_t pid, uint32_t period_ms, uint32_t now);

    // Mit csinál: ha van esedékes PID, a legjobban későt adja vissza.
    // Hogyan:     késés = now - esedékes időpont; a rangsor késés/periódus (ezrelék).
    // Miért:      külön függvény, hogy a választási szabály ECU nélkül is tesztelhető legyen.
    bool next_due(uint32_t now, uint8_t& pid) const;

    // Mit csinál: legfeljebb egy lekérdezést végez. Igaz, ha küldött kérést (err = eredmény).
    // Hogyan:     next_due -> read_pid -> dekódolás -> időzítés előreléptetése. A kihagyott
    //             ciklusokat nem pótolja (nincs "sorozatlövés" egy hosszabb szünet után).
    // Miért:      a hívó ciklus (task) maga dönt az alvásról: ha false, alhat ms_until_next()-et.
    bool step(ObdClient& client, Err& err);

    // Hány ms múlva esedékes a következő lekérdezés (0, ha már esedékes; 0xFFFFFFFF, ha üres).
    uint32_t ms_until_next(uint32_t now) const;

    // Mit csinál: a PID utolsó sikeres értékét adja. age_ms: mennyi ideje volt sikeres.
    // Hamis, ha még nem volt egyetlen sikeres olvasás sem.
    // Miért: a megjelenítő az életkorból dönt az "elavult" jelzésről (500 ms-nál régebbi).
    bool value(uint8_t pid, float& v, uint32_t now, uint32_t& age_ms) const;

    // A PID 01 01 alapján: MIL és hibakódok száma (érvényes, ha a PID sikeresen olvasva volt).
    bool mil(MilStatus& m) const;

    // A PID frissítési periódusa (ms); 0, ha nincs felvéve. A frissesség megítéléséhez kell.
    uint32_t period(uint8_t pid) const;

    // Statisztika (diagnosztika és tesztek).
    uint32_t requests(uint8_t pid) const;
    uint32_t failures(uint8_t pid) const;
    uint32_t consecutive_failures(uint8_t pid) const;

private:
    struct Entry {
        uint8_t pid = 0;
        uint32_t period_ms = 0;
        uint32_t next_due = 0;
        float value = 0.f;
        uint8_t raw_a = 0;  // a PID 01 01 nyers A bájtja
        uint32_t last_ok_ms = 0;
        bool has_value = false;
        uint32_t total = 0, failed = 0, consec_failed = 0;
    };
    const Entry* find(uint8_t pid) const;
    Entry* find(uint8_t pid);

    Entry e_[kMaxEntries];
    size_t n_ = 0;
};

}  // namespace og
