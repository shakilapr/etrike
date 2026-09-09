// etrike_sim scenario runner — Milestone vertical slice (Phase A + B).
//
// Scenarios are deterministic (SimClock + virtual CAN + real codecs):
//   normal_drive       Host 0x300 -> RT 0x204 -> MTR actuation (DAC > 0).
//   estop_reset_rearm  SYS ESTOP press -> RT/MTR latch, DAC 0 -> operator
//                      reset -> two-frame 0x011 clear -> 0x113 OFF->ON REARM
//                      -> AUTO -> fresh 0x300 -> motion resumes.
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "vehicle/vehicle_sim.h"

using sim::VehicleSim;
using VehicleSnapshot = sim::VehicleSim::VehicleSnapshot;

static int g_pass = 0;
static int g_fail = 0;
static const VehicleSim* g_vehicle = nullptr;  // set per scenario for trace dump

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            ++g_pass;                                                      \
        } else {                                                           \
            ++g_fail;                                                      \
            std::fprintf(stderr, "  FAIL %s:%d : %s\n", __FILE__, __LINE__, \
                         #cond);                                           \
            if (g_vehicle) dump_trace_tail(*g_vehicle);                    \
        }                                                                  \
    } while (0)

namespace {

using Pred = std::function<bool(const VehicleSnapshot&)>;

void dump_trace_tail(const VehicleSim& v);  // defined below

// Run in 10 ms quanta until `pred` holds or `timeout_ms` elapses.
bool poll(VehicleSim& v, int64_t timeout_ms, Pred pred) {
    int64_t elapsed = 0;
    while (elapsed < timeout_ms) {
        v.run_for(10);
        elapsed += 10;
        if (pred(v.snapshot())) return true;
    }
    return false;
}

// Bring the vehicle to "all authority streams active, AUTO, driving".
// Returns false if any stage timed out.
bool drive_to_active(VehicleSim& v, int32_t speed_mmps) {
    if (!poll(v, 1500, [](const VehicleSnapshot& s) {
            return !s.rt.no_sys_authority;  // 0x011 acquired (issue #10)
        })) {
        std::fprintf(stderr, "  [drive_to_active] authority never acquired\n");
        return false;
    }
    v.command_speed(speed_mmps);
    v.toggle_mode();  // Manual -> AUTO (0x110 authority)
    return poll(v, 2500, [](const VehicleSnapshot& s) {
        return s.rt.tx_speed_mmps != 0 && s.mtr_dac > 0 &&
               !s.rt.estop_active;
    });
}

bool drive_stays_zero(VehicleSim& v, int64_t hold_ms) {
    v.run_for(hold_ms);
    return v.snapshot().rt.tx_speed_mmps == 0 && v.snapshot().mtr_dac == 0;
}

void dump_trace_tail(const VehicleSim& v) {
    const auto& ev = v.trace().events();
    const std::size_t n = ev.size();
    const std::size_t from = n > 120 ? n - 120 : 0;
    std::fprintf(stderr, "  --- trace tail (%zu of %zu events) ---\n", n - from,
                 n);
    for (std::size_t i = from; i < n; ++i) {
        std::fprintf(stderr, "  %8lld us  %-4s %-16s %s\n",
                     static_cast<long long>(ev[i].time_us),
                     ev[i].component.c_str(), ev[i].event.c_str(),
                     ev[i].detail.c_str());
    }
}

void scenario_normal_drive() {
    std::printf("[normal_drive] Host 0x300 -> RT 0x204 -> MTR DAC\n");
    VehicleSim v;
    v.boot();
    g_vehicle = &v;
    CHECK(drive_to_active(v, 2000));

    const VehicleSnapshot s = v.snapshot();
    CHECK(s.rt.mode == sim::RtCore::kModeAuto);
    CHECK(!s.rt.estop_active);
    CHECK(!s.sys_estop);
    CHECK(s.mtr_dac > 0);
    CHECK(s.rt.tx_speed_mmps > 0);
    // Keep driving: motion must persist (heartbeats + 0x011 stay fresh).
    v.run_for(800);
    const VehicleSnapshot s2 = v.snapshot();
    CHECK(!s2.rt.estop_active && s2.mtr_dac > 0);
    CHECK(v.trace().count("rt", "tx_0x204") > 0);
    g_vehicle = nullptr;
}

void scenario_estop_reset_rearm() {
    std::printf("[estop_reset_rearm] ESTOP -> reset -> 0x113 REARM -> motion\n");
    VehicleSim v;
    v.boot();
    g_vehicle = &v;
    CHECK(drive_to_active(v, 2000));

    // 1. SYS ESTOP GPIO press -> RT + MTR latch, DAC cut to 0.
    v.press_estop();
    CHECK(poll(v, 800, [](const VehicleSnapshot& s) {
        return s.rt.estop_active && s.mtr_estop && s.mtr_dac == 0;
    }));
    CHECK(v.snapshot().rt.tx_speed_mmps == 0);

    // 2. Button release alone does NOT clear (SYS SW latch persists).
    v.release_estop();
    v.run_for(400);
    CHECK(v.snapshot().rt.estop_active);
    CHECK(v.snapshot().mtr_estop);

    // 3. Operator reset (START). RT clears on the two advancing 0x011 zeros;
    //    MTR clears into REARM_REQUIRED (still stopped).
    v.press_start();
    CHECK(poll(v, 1200, [](const VehicleSnapshot& s) {
        return !s.rt.estop_active && !s.mtr_estop;
    }));
    CHECK(v.snapshot().mtr_rearm_required);
    CHECK(drive_stays_zero(v, 400));  // fresh 0x300 cannot restore motion yet

    // 4. REARM via 0x113 OFF->ON.
    v.cycle_mtr_power();
    CHECK(!v.snapshot().mtr_rearm_required);

    // 5. Re-enter AUTO; fresh drive command restores motion.
    v.toggle_mode();
    CHECK(poll(v, 2500, [](const VehicleSnapshot& s) {
        return s.rt.tx_speed_mmps != 0 && s.mtr_dac > 0;
    }));
    CHECK(!v.snapshot().rt.estop_active);
    CHECK(v.snapshot().mtr_dac > 0);
    g_vehicle = nullptr;
}

}  // namespace

int main() {
    std::printf("=== etrike_sim full-system scenarios ===\n");

    scenario_normal_drive();
    scenario_estop_reset_rearm();

    std::printf("=== %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
