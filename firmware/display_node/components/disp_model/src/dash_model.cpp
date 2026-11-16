#include "dash_model.h"

#include <cstring>

#include "og_codec.h"
#include "og_vin.h"

namespace og {

bool DashModel::on_packet(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms) {
    // Az életjel nem hiba és nem telemetria: csak a kapcsolat élő voltát jelzi, a csomagstatisztikát nem érinti.
    HeartbeatMsg hb;
    if (decode(data, len, hb)) {
        heard_ = true;
        last_heard_ms_ = now_ms;
        return false;
    }
    SleepMsg sl;
    if (decode(data, len, sl)) {
        heard_ = true;
        last_heard_ms_ = now_ms;
        if (sl.reason == static_cast<uint8_t>(SleepReason::EngineStopped)) {
            engine_stopped_ = true;
            stopped_ms_ = now_ms;
        }
        return false;
    }
    VinMsg vm;
    if (decode(data, len, vm)) {  // a VIN nem telemetria: a csomagstatisztikába nem számít
        heard_ = true;
        last_heard_ms_ = now_ms;
        vm.vin[kVinLen] = 0;
        if (vin_chars_valid(vm.vin) && std::strcmp(vm.vin, vin_) != 0) {
            std::memcpy(vin_, vm.vin, sizeof vin_);
            vin_dirty_ = true;
        }
        return false;
    }
    TelemetryMsg m;
    if (!decode(data, len, m)) {
        ++invalid_;
        return false;
    }
    heard_ = true;
    last_heard_ms_ = now_ms;
    ++sec_count_;
    sec_rssi_sum_ += rssi;
    if (!stats_.on_seq(m.h.seq)) return false;  // duplikátum vagy késve érkezett

    engine_stopped_ = false;  // új adat: a motor megint jár
    last_ = m;
    has_data_ = true;
    last_ms_ = now_ms;
    if (m.flags & kFlagRpm) rpm_.push(m.rpm_q4 / 4.f, now_ms);
    if (m.flags & kFlagSpeed) speed_.push(static_cast<float>(m.speed_kmh), now_ms);
    return true;
}

void DashModel::on_second() {
    per_sec_ = sec_count_;
    rssi_avg_ = sec_count_ ? static_cast<int>(sec_rssi_sum_ / static_cast<int32_t>(sec_count_)) : 0;
    sec_count_ = 0;
    sec_rssi_sum_ = 0;

    // A statisztika újraszinkronizáláskor nullázódik: ilyenkor az összesítő kisebb lesz, és a változás maga az összesítő.
    const uint32_t rx = stats_.received(), lo = stats_.lost();
    const bool reset = rx < win_prev_rx_ || lo < win_prev_lost_;
    win_rx_[win_pos_] = reset ? rx : rx - win_prev_rx_;
    win_lost_[win_pos_] = reset ? lo : lo - win_prev_lost_;
    win_pos_ = (win_pos_ + 1) % kWindowS;
    win_prev_rx_ = rx;
    win_prev_lost_ = lo;
}

DashValues DashModel::values(uint32_t now_ms) const {
    DashValues v;
    v.has_data = has_data_;
    if (!has_data_) return v;
    v.age_ms = now_ms - last_ms_;
    v.fresh = v.age_ms <= kStaleMs;
    v.flags = last_.flags;
    v.rpm = rpm_.value(now_ms);
    v.speed_kmh = speed_.value(now_ms);
    v.coolant_c = last_.coolant_c;
    v.throttle_pct = last_.throttle_raw * 100.f / 255.f;
    v.vbat_v = last_.vbat_mv / 1000.f;
    v.mil_on = (last_.mil_dtc & 0x80) != 0;
    v.dtc_count = last_.mil_dtc & 0x7F;
    return v;
}

DashLink DashModel::link() const {
    DashLink l;
    l.received = stats_.received();
    l.lost = stats_.lost();
    l.stale = stats_.stale();
    l.resyncs = stats_.resyncs();
    l.invalid = invalid_;
    l.loss_ratio = stats_.loss_ratio();
    uint32_t wrx = 0, wlo = 0;
    for (int i = 0; i < kWindowS; ++i) {
        wrx += win_rx_[i];
        wlo += win_lost_[i];
    }
    l.loss_ratio_window = (wrx + wlo) ? static_cast<float>(wlo) / static_cast<float>(wrx + wlo) : 0.f;
    l.last_seq = stats_.last_seq();
    l.per_sec = per_sec_;
    l.rssi_dbm = rssi_avg_;
    return l;
}

}  // namespace og
