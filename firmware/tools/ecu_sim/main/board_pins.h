#pragma once
// ECU-szimulátor lábkiosztása, hardverváltozatonként.
//
// Mit csinál: megadja, melyik GPIO-ra csatlakozik a CAN-adó-vevő modul.
// Miért:      a lábak csak itt szerepelnek, a program többi része hardverfüggetlen.
//
// Jelenleg egyetlen változat van: ESP32-S2 mini fejlesztőpanel + SN65HVD230 CAN-TTL modul.
// (Az asztali tesztkörnyezet része; a szimulátorhoz nem készül saját NYÁK.)

namespace og::board {

constexpr int kPinCanTx = 2;  // a modul TXD/CTX bemenete
constexpr int kPinCanRx = 1;  // a modul RXD/CRX kimenete

}  // namespace og::board
