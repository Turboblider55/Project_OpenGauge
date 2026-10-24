#pragma once
// Szimulált CAN-busz PC-s teszteléshez (virtuális órával).
//
// Mit csinál: egy tesztert és legfeljebb négy virtuális ECU-t köt össze memóriában.
// Hogyan:     a send() minden csatlakoztatott ECU-nak továbbadja a keretet; az ECU válaszai
//             esedékességi idővel (most + késleltetés) kerülnek egy sorba; a recv() a
//             virtuális órát a következő esedékes keretig (vagy a határidőig) előreteszi.
// Miért:      így a tesztek valós várakozás nélkül, determinisztikusan futnak, a P2-időzítések
//             és az időtúllépések pontosan ellenőrizhetők. A keret átviteli idejét
//             (kb. 0,27 ms 500 kbit/s-nál) nem modellezzük: ms-os felbontásban elhanyagolható.
//
// Hibabeállítások: bus_bitrate (más sebességnél a keret nem kap nyugtát: send() hamis),
// rx_hook (a tesztelőnek szánt keretek eldobása/módosítása), inject() (idegen forgalom).
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "can_iface.h"
#include "ecu_model.h"

namespace og {

class VirtualBus : public ICanPort, private Emitter {
public:
    explicit VirtualBus(uint32_t bus_bitrate_bps = 500000, uint32_t start_ms = 0)
        : bus_bitrate_(bus_bitrate_bps), now_(start_ms) {}

    // ECU csatlakoztatása (a busz nem birtokolja, a hívó kezeli az élettartamát).
    void attach(EcuModel* ecu) { ecus_.push_back(ecu); }

    // A busz tényleges sebessége; a teszter csak ugyanezzel a sebességgel kommunikálhat.
    void set_bus_bitrate(uint32_t bps) { bus_bitrate_ = bps; }

    // Hibainjektálás: a tesztelő felé tartó keretet a hook módosíthatja vagy eldobhatja.
    std::function<void(CanFrame&, bool& drop)> rx_hook;

    // Idegen forgalom (pl. más vezérlő üzenete) a tesztelő felé, delay_ms múlva.
    void inject(const CanFrame& f, uint32_t delay_ms) { push(f, delay_ms); }

    // A teszter által eddig küldött keretek (ellenőrzéshez).
    const std::vector<CanFrame>& tx_log() const { return tx_log_; }
    void clear_log() { tx_log_.clear(); }

    // ICanPort
    bool configure(const CanConfig& cfg) override {
        cfg_ = cfg;
        return true;
    }

    // Mit csinál: keretet ad a buszra. Hogyan: ha a sebesség eltér a buszétól vagy csak
    // hallgató módban vagyunk, nincs nyugtázás -> hamis (valós TWAI-nál is hibával jár).
    bool send(const CanFrame& f, uint32_t timeout_ms) override {
        if (cfg_.listen_only || cfg_.bitrate_bps != bus_bitrate_) {
            now_ += timeout_ms;  // a valós driver a határidőig próbálkozna
            return false;
        }
        tx_log_.push_back(f);
        for (EcuModel* e : ecus_) e->on_frame(f, *this);
        return true;
    }

    // Mit csinál: a következő esedékes keretet adja, a virtuális órát előreléptetve.
    bool recv(CanFrame& f, uint32_t timeout_ms) override {
        if (cfg_.bitrate_bps != bus_bitrate_) {  // rossz sebességnél csak hibakeretek lennének
            now_ += timeout_ms;
            return false;
        }
        size_t best = q_.size();
        for (size_t i = 0; i < q_.size(); ++i)  // legkorábbi esedékes (azonosnál a sorrend)
            if (best == q_.size() || diff(q_[i].due, q_[best].due) < 0) best = i;
        if (best != q_.size() && diff(q_[best].due, now_) <= static_cast<int32_t>(timeout_ms)) {
            if (diff(q_[best].due, now_) > 0) now_ = q_[best].due;
            f = q_[best].frame;
            q_.erase(q_.begin() + static_cast<long>(best));
            return true;
        }
        now_ += timeout_ms;
        return false;
    }

    uint32_t now_ms() override { return now_; }

private:
    struct Item {
        CanFrame frame;
        uint32_t due;
    };
    // Előjeles időkülönbség: az átforduló 32 bites óra mellett is helyes.
    static int32_t diff(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b); }

    void push(const CanFrame& f, uint32_t delay_ms) {
        CanFrame g = f;
        bool drop = false;
        if (rx_hook) rx_hook(g, drop);
        if (!drop) q_.push_back(Item{g, now_ + delay_ms});
    }
    void emit(const CanFrame& f, uint32_t delay_ms) override { push(f, delay_ms); }

    uint32_t bus_bitrate_;
    uint32_t now_;
    CanConfig cfg_;
    std::vector<EcuModel*> ecus_;
    std::deque<Item> q_;
    std::vector<CanFrame> tx_log_;
};

}  // namespace og
