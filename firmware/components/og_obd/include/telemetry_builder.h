#pragma once
// A PID-ütemező értékeiből az ESP-NOW-n küldött TelemetryMsg összeállítása.
//
// Mit csinál: a PidScheduler utolsó értékeit a protokoll fix pontos mezőibe írja, és a flags
//             mezőben jelzi, mely értékek érvényesek.
// Hogyan:     egy mező csak akkor érvényes, ha a PID-et támogatja az ECU, legalább egyszer
//             sikerült kiolvasni, és az érték nem régebbi a periódusa kétszeresénél.
// Miért:      a kijelző a flags alapján dönti el, mit mutat "nincs adat"-ként; egy elakadt PID
//             régi értéke így nem látszik frissnek. Hardverfüggetlen, PC-n tesztelhető.
#include <cstdint>

#include "og_protocol.h"
#include "pid_scheduler.h"

namespace og {

TelemetryMsg build_telemetry(const PidScheduler& s, uint32_t now_ms, uint16_t seq);

}  // namespace og
