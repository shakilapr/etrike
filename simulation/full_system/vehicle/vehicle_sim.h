#pragma once
// VehicleSim — deterministic full-system facade.
//
// Boots RT/SYS/MTR/Host, runs real frames over the virtual low/high CAN
// fabric on a 1 ms quantum, and exposes operator actions + a snapshot + a
// trace for scenario assertions.
#include <cstdint>
#include <string>

#include "core/rt_core.h"
#include "models/host_model.h"
#include "models/mtr_model.h"
#include "models/sys_model.h"
#include "runtime/sim_clock.h"
#include "runtime/trace.h"
#include "runtime/virtual_can.h"

namespace sim {

class VehicleSim {
public:
    struct VehicleSnapshot {
        RtCore::Snapshot rt;
        bool sys_estop = false;
        bool sys_auto = false;
        bool mtr_estop = false;
        bool mtr_rearm_required = false;
        int  mtr_dac = 0;
        int32_t mtr_applied_speed_mmps = 0;
        rt::SafetyStreamState authority_state =
            rt::SafetyStreamState::UNACQUIRED;
    };

    VehicleSim() {
        // Wire the RT core to both buses (RT is dual-bus).
        bus_.subscribe(Bus::Low, [this](int64_t t, const auto& f) {
            rt_.on_frame(false, f, t);
        });
        bus_.subscribe(Bus::High, [this](int64_t t, const auto& f) {
            rt_.on_frame(true, f, t);
        });
        bus_.subscribe(Bus::Low, [this](int64_t t, const auto& f) {
            mtr_.on_frame(f, t);
        });
        host_.attach(bus_);
        // RT -> low-bus TX sink (0x204 / keep-alive) with trace capture.
        rt_.set_low_tx_cb([this](const etrike::protocol::Frame& fr) {
            bus_.transmit(Bus::Low, fr);
            if (fr.id == can::kIdRtDriveCmd) {
                can::gen::RtDriveCmd dc{};
                if (can::decode_frame(fr.view(), dc) == can::gen::CodecStatus::Ok) {
                    trace_.record(clock_.now_us(), "rt", "tx_0x204",
                                  "speed=" + std::to_string(dc.motor_speed_mmps) +
                                      " gear=" + std::to_string(dc.gear),
                                  dc.motor_speed_mmps);
                }
            }
        });
    }

    void boot() {
        const int64_t t0 = 0;
        clock_.reset(t0);
        trace_.clear();
        rt_.boot(t0);
        host_.boot(t0);
        sys_.boot(t0);
        mtr_.boot(t0);
        steering_ok_since_us_ = t0;
        prev_rt_estop_ = false;
        prev_rt_no_auth_ = true;
        prev_mtr_estop_ = false;
        prev_dac_ = 0;
        booted_ = true;
    }

    // Advance simulated time by `duration_ms` in 1 ms quanta.
    void run_for(int64_t duration_ms) {
        for (int64_t i = 0; i < duration_ms; ++i) {
            clock_.advance_us(1000);
            const int64_t now = clock_.now_us();
            bus_.deliver(now);       // frames transmitted last quantum
            host_.publish(now, bus_);
            sys_.publish(now, bus_);
            rt_.step(now);
            update_steering_gate(now);
            mtr_.step(now, bus_);
            observe_transitions(now);
        }
    }

    // ---- operator / host actions ------------------------------------
    void press_estop() { sys_.set_estop_button(true); }
    void release_estop() { sys_.set_estop_button(false); }
    void press_start() { sys_.press_start(); }
    void toggle_mode() { sys_.press_mode(); }
    void command_speed(int32_t mmps) { host_.set_target_speed(mmps); }

    // MTR REARM: 0x113 OFF for 150 ms then ON for 150 ms (operator service
    // action; a real SYS issues this on a restart cycle).
    void cycle_mtr_power() {
        sys_.set_power_request(false);
        run_for(150);
        sys_.set_power_request(true);
        run_for(150);
    }

    VehicleSnapshot snapshot() const noexcept {
        VehicleSnapshot s{};
        s.rt = rt_.snapshot();
        s.sys_estop = sys_.estop_active();
        s.sys_auto = sys_.mode_auto();
        s.mtr_estop = mtr_.is_estop_active();
        s.mtr_rearm_required = mtr_.rearm_required();
        s.mtr_dac = mtr_.dac();
        s.mtr_applied_speed_mmps = mtr_.applied_speed_mmps();
        s.authority_state = rt_.authority_state();
        return s;
    }

    Trace& trace() noexcept { return trace_; }
    const Trace& trace() const noexcept { return trace_; }
    RtCore& rt() noexcept { return rt_; }
    SimClock& clock() noexcept { return clock_; }

private:
    void update_steering_gate(int64_t now) {
        // Steering-readiness seam (Phase G replaces this model with the real
        // EPS/SES acquisition). Model: EPS starts ready after 500 ms boot wait
        // and re-engages 150 ms after an estop release (ramp/hold settling).
        const bool rt_estop = rt_.estop_active();
        if (!rt_estop && steering_ok_since_us_ == -1) steering_ok_since_us_ = now;
        if (rt_estop) steering_ok_since_us_ = -1;
        const bool ready =
            steering_ok_since_us_ >= 0 &&
            (now - steering_ok_since_us_) >= (rt_estop ? 0 : 150'000) &&
            now >= 500'000 && !rt_estop;
        rt_.set_steering_active(ready);
    }

    void observe_transitions(int64_t now) {
        const auto s = snapshot();
        if (s.rt.estop_active != prev_rt_estop_) {
            trace_.record(now, "rt",
                          s.rt.estop_active ? "estop_set" : "estop_clear",
                          "reason=" + std::to_string(s.rt.estop_reason),
                          s.rt.estop_reason);
            prev_rt_estop_ = s.rt.estop_active;
        }
        if (s.rt.no_sys_authority != prev_rt_no_auth_) {
            trace_.record(now, "rt",
                          s.rt.no_sys_authority ? "authority_lost"
                                                : "authority_acquired");
            prev_rt_no_auth_ = s.rt.no_sys_authority;
        }
        if (s.mtr_estop != prev_mtr_estop_) {
            trace_.record(now, "mtr",
                          s.mtr_estop ? "estop_set" : "estop_clear");
            prev_mtr_estop_ = s.mtr_estop;
        }
        if ((s.mtr_dac == 0) != (prev_dac_ == 0) || s.mtr_dac != prev_dac_) {
            trace_.record(now, "mtr",
                          s.mtr_dac == 0 ? "dac_zero" : "dac_active",
                          "dac=" + std::to_string(s.mtr_dac), s.mtr_dac);
            prev_dac_ = s.mtr_dac;
        }
    }

    SimClock clock_;
    VirtualCanBus bus_;
    HostModel host_;
    SysModel sys_;
    MtrModel mtr_;
    RtCore rt_;

    bool booted_ = false;
    int64_t steering_ok_since_us_ = 0;
    bool prev_rt_estop_ = false;
    bool prev_rt_no_auth_ = true;
    bool prev_mtr_estop_ = false;
    int prev_dac_ = 0;
    Trace trace_;
};

}  // namespace sim
