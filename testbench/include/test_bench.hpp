#pragma once

#include <cstdint>
#include <memory>
#include "sim_clock.hpp"
#include "virtual_can_bus.hpp"
#include "nodes/host_model.hpp"
#include "nodes/rt_node.hpp"
#include "nodes/sys_node.hpp"
#include "nodes/mtr_node.hpp"
#include "nodes/seb_model.hpp"
#include "nodes/ses_model.hpp"
#include "nodes/rm_operator_model.hpp"

namespace testbench {

class TestBench {
public:
    TestBench();
    ~TestBench() = default;

    void boot();
    void reset();

    // Advance simulation time deterministically
    void step(uint32_t dt_ms = 1);
    void run_for_ms(uint32_t duration_ms, uint32_t step_dt_ms = 1);

    // Node accessors
    HostModel& host() { return host_; }
    RtNode& rt() { return rt_; }
    SysNode& sys() { return sys_; }
    MtrNode& mtr() { return mtr_; }
    SebModel& seb() { return seb_; }
    SesModel& ses() { return ses_; }
    RmOperatorModel& rm() { return rm_; }

    // Bus accessors (with fault injection)
    VirtualCanBus& high_can() { return high_can_; }
    VirtualCanBus& low_can() { return low_can_; }
    SimClock& clock() { return clock_; }

    // Operator physical inputs
    void press_start_button();
    void press_mode_button();
    void hold_mode_button_3s();
    void press_estop_button();
    void release_estop_button();
    void power_off();
    void power_on();

private:
    SimClock clock_;
    VirtualCanBus high_can_;
    VirtualCanBus low_can_;

    HostModel host_;
    RtNode rt_;
    SysNode sys_;
    MtrNode mtr_;
    SebModel seb_;
    SesModel ses_;
    RmOperatorModel rm_;

    void setup_routing();
};

} // namespace testbench
