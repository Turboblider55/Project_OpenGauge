#include "pid_scheduler.h"
#include <climits>

namespace og {

namespace {
inline int32_t diff(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b); }
}

const PidScheduler::Entry* PidScheduler::find(uint8_t pid) const {
    for (size_t i = 0; i < n_; ++i)
        if (e_[i].pid == pid) return &e_[i];
    return nullptr;
}
PidScheduler::Entry* PidScheduler::find(uint8_t pid) {
    for (size_t i = 0; i < n_; ++i)
        if (e_[i].pid == pid) return &e_[i];
    return nullptr;
}

bool PidScheduler::add(uint8_t pid, uint32_t period_ms, uint32_t now) {
    if (n_ >= kMaxEntries || period_ms == 0 || find(pid) != nullptr) return false;
    Entry& e = e_[n_++];
    e = Entry();
    e.pid = pid;
    e.period_ms = period_ms;
    e.next_due = now;
    return true;
}

bool PidScheduler::next_due(uint32_t now, uint8_t& pid) const {
    bool found = false;
    int64_t best = -1;
    for (size_t i = 0; i < n_; ++i) {
        const int32_t late = diff(now, e_[i].next_due);
        if (late < 0) continue;  // még nem esedékes
        // Késés a periódus ezrelékében; 64 bites, hogy ne csússzon túl.
        const int64_t score = static_cast<int64_t>(late) * 1000 / e_[i].period_ms;
        if (score > best) {  // azonos pontszámnál a korábban felvett (sorrendi) PID nyer
            best = score;
            pid = e_[i].pid;
            found = true;
        }
    }
    return found;
}

bool PidScheduler::step(ObdClient& client, Err& err) {
    uint8_t pid = 0;
    if (!next_due(client.now_ms(), pid)) return false;
    Entry* e = find(pid);

    uint8_t data[8];
    size_t len = 0;
    err = client.read_pid(pid, data, sizeof data, len);
    const uint32_t done = client.now_ms();  // a kérés befejezésének ideje

    ++e->total;
    if (err == Err::Ok) {
        bool decoded = false;
        if (pid == 0x01) {
            e->raw_a = data[0];
            decoded = len >= 1;
        } else {
            decoded = pid_decode(pid, data, len, e->value);
        }
        if (decoded) {
            e->has_value = true;
            e->last_ok_ms = done;
            e->consec_failed = 0;
        } else {
            err = Err::ProtocolError;  // ismeretlen PID vagy kevés adatbájt
        }
    }
    if (err != Err::Ok) {
        ++e->failed;
        ++e->consec_failed;
    }

    // Következő esedékesség: a periódus rácsán marad; ha több mint egy periódust késtünk
    // (pl. hosszú időtúllépés), nem pótoljuk a kihagyottakat, hanem "most"-ról folytatjuk.
    e->next_due += e->period_ms;
    if (diff(done, e->next_due) > static_cast<int32_t>(e->period_ms)) e->next_due = done;
    return true;
}

uint32_t PidScheduler::ms_until_next(uint32_t now) const {
    if (n_ == 0) return 0xFFFFFFFFu;
    int32_t best = INT32_MAX;
    for (size_t i = 0; i < n_; ++i) {
        const int32_t d = diff(e_[i].next_due, now);
        if (d < best) best = d;
    }
    return best > 0 ? static_cast<uint32_t>(best) : 0u;
}

bool PidScheduler::value(uint8_t pid, float& v, uint32_t now, uint32_t& age_ms) const {
    const Entry* e = find(pid);
    if (e == nullptr || !e->has_value) return false;
    v = e->value;
    age_ms = static_cast<uint32_t>(diff(now, e->last_ok_ms));
    return true;
}

bool PidScheduler::mil(MilStatus& m) const {
    const Entry* e = find(0x01);
    if (e == nullptr || !e->has_value) return false;
    m = decode_mil_status(e->raw_a);
    return true;
}

uint32_t PidScheduler::period(uint8_t pid) const {
    const Entry* e = find(pid);
    return e ? e->period_ms : 0;
}

uint32_t PidScheduler::requests(uint8_t pid) const {
    const Entry* e = find(pid);
    return e ? e->total : 0;
}
uint32_t PidScheduler::failures(uint8_t pid) const {
    const Entry* e = find(pid);
    return e ? e->failed : 0;
}
uint32_t PidScheduler::consecutive_failures(uint8_t pid) const {
    const Entry* e = find(pid);
    return e ? e->consec_failed : 0;
}

}  // namespace og
