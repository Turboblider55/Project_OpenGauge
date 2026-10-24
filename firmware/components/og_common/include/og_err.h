#pragma once
// Közös hibakódok (hardverfüggetlen, PC-n is fordítható).
namespace og {
enum class Err : int {
    Ok = 0,
    Timeout,
    InvalidArg,
    BadLength,
    BadVersion,
    BadType,
    BusError,
    NotSupported,
    SequenceError,     // hibás ISO-TP sorszám (hiányzó vagy kétszeres keret)
    BufferTooSmall,    // a fogadó puffer kisebb, mint a beérkező üzenet
    NegativeResponse,  // az ECU negatív választ adott (a kód külön lekérdezhető)
    ProtocolError,     // értelmezhetetlen vagy szabálytalan keret/üzenet
};

// Naplózáshoz: a hibakód rövid neve.
inline const char* err_name(Err e) {
    switch (e) {
        case Err::Ok: return "Ok";
        case Err::Timeout: return "Timeout";
        case Err::InvalidArg: return "InvalidArg";
        case Err::BadLength: return "BadLength";
        case Err::BadVersion: return "BadVersion";
        case Err::BadType: return "BadType";
        case Err::BusError: return "BusError";
        case Err::NotSupported: return "NotSupported";
        case Err::SequenceError: return "SequenceError";
        case Err::BufferTooSmall: return "BufferTooSmall";
        case Err::NegativeResponse: return "NegativeResponse";
        case Err::ProtocolError: return "ProtocolError";
    }
    return "?";
}
}  // namespace og
