#pragma once
// SAE J1979 szolgáltatás-üzenetek összeállítása és értelmezése (tiszta függvények).
//
// Mit csinál: kérésüzenetet épít (Mode 01/09/03/UDS-felismerő), a választ ellenőrzi és
//             kibontja (pozitív/negatív válasz, PID-egyezés, VIN, hibakódok száma).
// Hogyan:     csak bájtokkal dolgozik, a buszról és az ISO-TP-ről nem tud.
// Miért:      külön, hardver nélkül tesztelhető rétegben van a bájt-szintű protokoll, így az
//             ObdClient csak az átvitelt és az időzítést intézi.
#include <cstddef>
#include <cstdint>

#include "og_err.h"

namespace og {

constexpr uint8_t kSidCurrentData = 0x01;
constexpr uint8_t kSidStoredDtc = 0x03;
constexpr uint8_t kSidVehicleInfo = 0x09;
constexpr uint8_t kSidUdsReadDid = 0x22;
constexpr uint8_t kNrcResponsePending = 0x78;  // negatív válasz: "dolgozom, várj még"

// Mode 01 kérés: [01, pid]. Visszaadja a hosszt (2).
size_t obd_build_mode01(uint8_t pid, uint8_t* out);
// Mode 09 kérés: [09, pid]. Visszaadja a hosszt (2).
size_t obd_build_mode09(uint8_t pid, uint8_t* out);
// Mode 03 kérés: [03]. Visszaadja a hosszt (1).
size_t obd_build_mode03(uint8_t* out);
// UDS ReadDataByIdentifier: [22, did_hi, did_lo]. Visszaadja a hosszt (3).
size_t obd_build_uds_read_did(uint16_t did, uint8_t* out);

// Mit csinál: megmondja, hogy az üzenet negatív válasz-e (7F sid nrc).
// Hogyan:     az első bájt 0x7F és legalább 3 bájt; kiadja a kérés-SID-et és a kódot.
// Miért:      a kliensnek a "response pending" (0x78) kódot külön kell kezelnie.
bool obd_is_negative(const uint8_t* msg, size_t len, uint8_t& sid, uint8_t& nrc);

// Mit csinál: egy Mode 01 válaszból kiveszi az adatbájtokat.
// Hogyan:     [41, pid, A, B, ...] formát vár, és a pid egyezését ellenőrzi.
// Miért:      egy késve érkezett, más PID-hez tartozó válasz ne kerüljön rossz mezőbe.
// Hibák:      ProtocolError (nem 41 / más PID), BadLength (nincs adat).
Err obd_parse_mode01(const uint8_t* msg, size_t len, uint8_t pid, const uint8_t** data, size_t& data_len);

// Támogatott-PID bitmap (4 bájt big-endian) a 01 00/20/40 válaszból.
Err obd_parse_supported(const uint8_t* msg, size_t len, uint8_t base, uint32_t& bitmap);

// VIN a Mode 09 PID 02 válaszból: [49, 02, 01, 17 karakter]. Az out 18 bájt (lezáró 0-val).
// A karaktereket ellenőrzi: nagybetű/számjegy, I, O, Q nélkül (ISO 3779).
Err obd_parse_vin(const uint8_t* msg, size_t len, char out[18]);

// A Mode 03 válaszból a hibakódok darabszáma: [43, n, ...]. (Párok kibontása: P3.)
Err obd_parse_dtc_count(const uint8_t* msg, size_t len, uint8_t& count);

// OBDonUDS felismerés: a 22 F8 10 pozitív válasza [62, F8, 10, ...]. Igaz, ha ilyen.
bool obd_is_uds_protocol_id_response(const uint8_t* msg, size_t len);

}  // namespace og
