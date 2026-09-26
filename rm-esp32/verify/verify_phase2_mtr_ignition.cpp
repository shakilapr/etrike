// Phase 2 (BARE scenario) — Does rm BARE mode drive the MTR actuator seamlessly?
//
// mtr-stm32/src/motor_manager.h gates ignition as:
//   ignition_on_ = power_valid_ && power_state_on_ && safety_state_valid_ && mode_valid_
//   (lines 313 / 323) and reports authority loss in its node-status mask
//   (lines 510-516). Crucially, safety_state_valid_ is set ONLY by handling
//   0x011 SYS_SAFETY_STS (handle_safety_status, :194-212) at 700 ms freshness.
// rm BARE mode (src/can_emitter.h) emits 0x169/0x7B9/0x204/0x110/0x113 but NOT
// 0x011. This harness replicates the EXACT gating booleans from motor_manager.h
// and feeds rm BARE-mode cadence to show the resulting ignition state.
//
// Build (repo root):
//   C:\TDM-GCC-64\bin\g++.exe -std=c++17 ^
//     rm-esp32-t12d/verify/verify_phase2_mtr_ignition.cpp -o rm-esp32-t12d/verify/verify_phase2_mtr_ignition.exe

#include <cstdint>
#include <cstdio>

namespace {

// Freshness windows copied from mtr-stm32/src/motor_manager.h
constexpr int64_t kAuthFreshUs = 500'000;   // SysModeCmd::kCycleMs(100)*5
constexpr int64_t kSafetyFreshUs = 700'000; // kSafetyFreshMs

// Faithful replica of MTR authority gating (motor_manager.h:313/323/510-516).
struct MtrAuthority {
    bool mode_valid_ = false;          // from 0x110 (AUTO, fresh)
    bool power_valid_ = false;         // from 0x113 (REARM satisfied, fresh)
    bool power_state_on_ = false;      // from 0x113 power_state==ON
    bool safety_state_valid_ = false;  // from 0x011 ONLY (line 194-212)
    bool ignition_on_ = false;

    void tick(int64_t now, bool rx_110, bool rx_113_on, bool rx_011, bool rx_204) {
        static int64_t last_110 = -1, last_113 = -1, last_011 = -1;
        if (rx_110) last_110 = now;
        if (rx_113_on) last_113 = now;
        if (rx_011) last_011 = now;

        mode_valid_ = (last_110 >= 0) && (now - last_110) <= kAuthFreshUs;
        power_valid_ = (last_113 >= 0) && (now - last_113) <= kAuthFreshUs;
        power_state_on_ = (last_113 >= 0) && (now - last_113) <= kAuthFreshUs;
        safety_state_valid_ = (last_011 >= 0) && (now - last_011) <= kSafetyFreshUs;  // needs 0x011!

        ignition_on_ = power_valid_ && power_state_on_ && safety_state_valid_ && mode_valid_;
        (void)rx_204;  // 0x204 drive command is applied only if mode_valid_ (line 122-126)
    }
};

void simulate(const char* label, bool with_011) {
    MtrAuthority mtr;
    bool ever_ignited = false, ignited_at_2s = false;
    for (int64_t t = 0; t <= 3000'000; t += 10'000) {
        // rm BARE cadence (always): 0x110 @100ms, 0x113 @100ms, 0x204 @10ms
        bool rx_110 = (t % 100'000 == 0);
        bool rx_113_on = (t % 100'000 == 0);
        bool rx_204 = (t % 10'000 == 0);
        bool rx_011 = with_011 && (t % 200'000 == 0);  // SYS_SAFETY_STS @200ms (if present)
        mtr.tick(t, rx_110, rx_113_on, rx_011, rx_204);
        if (mtr.ignition_on_) ever_ignited = true;
        if (t >= 2000'000) ignited_at_2s = mtr.ignition_on_;
    }
    std::printf("  [%s] ignition_on(ever)=%s  ignited@2s=%s\n",
                label, ever_ignited ? "YES" : "NO", ignited_at_2s ? "YES" : "NO");
}

}  // namespace

int main() {
    std::printf("=== Phase 2 (BARE scenario) MTR ignition vs rm BARE mode ===\n");
    std::printf("-- rm BARE ONLY (0x110/0x113/0x204, NO 0x011) --\n");
    simulate("rm-BARE-only", /*with_011=*/false);
    std::printf("-- rm BARE + 0x011 SYS_SAFETY_STS @200ms --\n");
    simulate("rm-BARE+011", /*with_011=*/true);
    std::printf("NOTE: rm BARE mode does NOT emit 0x011 (src/can_emitter.h), so 'rm-BARE-only'\n");
    std::printf("      shows MTR never ignites -> DAC zeroed -> motor never moves.\n");
    return 0;
}
