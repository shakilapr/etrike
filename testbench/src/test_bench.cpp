#include "test_bench.hpp"

namespace testbench {

TestBench::TestBench()
    : clock_(100),
      high_can_("HIGH_CAN"),
      low_can_("LOW_CAN"),
      host_(high_can_),
      rt_(high_can_, low_can_),
      sys_(low_can_),
      mtr_(low_can_),
      seb_(low_can_),
      ses_(low_can_),
      rm_(low_can_) {
    setup_routing();
    boot();
}

void TestBench::setup_routing() {
    // High CAN subscribers
    high_can_.register_node(NodeId::HOST, [this](const etrike::protocol::Frame& f) {
        host_.receive_can("HIGH_CAN", f);
    });
    high_can_.register_node(NodeId::RT, [this](const etrike::protocol::Frame& f) {
        rt_.receive_can("HIGH_CAN", f);
    });

    // Low CAN subscribers
    low_can_.register_node(NodeId::RT, [this](const etrike::protocol::Frame& f) {
        rt_.receive_can("LOW_CAN", f);
    });
    low_can_.register_node(NodeId::SYS, [this](const etrike::protocol::Frame& f) {
        sys_.receive_can("LOW_CAN", f);
    });
    low_can_.register_node(NodeId::MTR, [this](const etrike::protocol::Frame& f) {
        mtr_.receive_can("LOW_CAN", f);
    });
    low_can_.register_node(NodeId::SEB, [this](const etrike::protocol::Frame& f) {
        seb_.receive_can("LOW_CAN", f);
    });
    low_can_.register_node(NodeId::SES, [this](const etrike::protocol::Frame& f) {
        ses_.receive_can("LOW_CAN", f);
    });
    low_can_.register_node(NodeId::RM, [this](const etrike::protocol::Frame& f) {
        rm_.receive_can("LOW_CAN", f);
    });
}

void TestBench::boot() {
    reset();
}

void TestBench::reset() {
    clock_.reset(100);
    high_can_.clear_faults();
    low_can_.clear_faults();
    high_can_.clear_trace();
    low_can_.clear_trace();

    host_.init();
    rt_.init();
    sys_.init();
    mtr_.init();
    seb_.init();
    ses_.init();
}

void TestBench::step(uint32_t dt_ms) {
    clock_.advance_ms(dt_ms);
    uint32_t now = clock_.now_ms();

    // Step ECU nodes and models
    host_.step(now, dt_ms);
    rt_.step(now, dt_ms);
    sys_.step(now, dt_ms);
    mtr_.step(now, dt_ms);
    seb_.step(now, dt_ms);
    ses_.step(now, dt_ms);
    rm_.step(now, dt_ms);

    // Deliver queued frames across buses
    high_can_.tick(now, dt_ms);
    low_can_.tick(now, dt_ms);
}

void TestBench::run_for_ms(uint32_t duration_ms, uint32_t step_dt_ms) {
    uint32_t elapsed = 0;
    while (elapsed < duration_ms) {
        step(step_dt_ms);
        elapsed += step_dt_ms;
    }
}

void TestBench::press_start_button() {
    sys_.press_start_button();
}

void TestBench::press_mode_button() {
    sys_.press_mode_button();
}

void TestBench::hold_mode_button_3s() {
    sys_.hold_mode_button_3s();
}

void TestBench::press_estop_button() {
    sys_.press_estop_button();
}

void TestBench::release_estop_button() {
    sys_.release_estop_button();
}

void TestBench::power_off() {
    host_.request_power(false);
}

void TestBench::power_on() {
    host_.request_power(true);
}

} // namespace testbench
