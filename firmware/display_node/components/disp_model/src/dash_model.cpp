#include "dash_model.h"

#include "og_codec.h"

namespace og {

bool DashModel::on_packet(const uint8_t* data, size_t len, int8_t rssi, uint32_t now_ms) {
    TelemetryMsg m;
    if (!decode(data, len, m)) {
        ++invalid_;
        return false;
    }
    ++sec_count_;
    sec_rssi_sum_ += rssi;
    if (!stats_.on_seq(m.h.seq)) return false;  // duplikátum vagy késve érkezett

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
    l.last_seq = stats_.last_seq();
    l.per_sec = per_sec_;
    l.rssi_dbm = rssi_avg_;
    return l;
}

}  // namespace og
