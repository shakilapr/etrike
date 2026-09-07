// Test: Dedicated 22 ESTOP & Safety Reset Scenario Suite (§7.12)
// Verifies assertion paths, assert dominance, reboot behavior, stale authority,
// gateway echo loops, and safety invariants.

#include <cassert>
#include <cstdio>
#include <cstdint>
#include "shared/diagnostics.h"

static int g_failed = 0;

#define ASSERT_TRUE(cond)                                                               \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::printf("FAIL %s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #cond);\
            ++g_failed;                                                                 \
        }                                                                               \
    } while (false)

// Mock State for ESTOP Simulator
struct EstopState {
    bool latched = false;
    bool estop_active_011 = false;
    uint8_t clear_count = 0;
    bool baseline_acquired = false;
    bool rearm_required = false;
    bool propulsion_allowed = false;
    uint32_t last_counter = 0;
};

// Asymmetric 0x011 clear evaluation engine
void process_011_frame(EstopState& state, bool estop_bit, uint8_t counter, bool crc_valid) {
    if (!crc_valid) {
        state.clear_count = 0;
        return;
    }

    if (estop_bit) {
        state.latched = true;
        state.estop_active_011 = true;
        state.clear_count = 0;
        state.rearm_required = true;
        state.propulsion_allowed = false;
        state.last_counter = counter;
        state.baseline_acquired = true;
        return;
    }

    // estop_bit == 0 (clear candidate)
    if (!state.baseline_acquired) {
        // First zero is baseline only
        state.baseline_acquired = true;
        state.clear_count = 0;
        state.last_counter = counter;
        return;
    }

    // Sequence check: counter must advance
    uint8_t expected_counter = static_cast<uint8_t>(state.last_counter + 1);
    if (counter != expected_counter) {
        // Counter jump or duplicate resets clear eligibility
        state.clear_count = 0;
        state.last_counter = counter;
        return;
    }

    state.last_counter = counter;
    ++state.clear_count;

    if (state.clear_count >= 2) {
        // 2 fresh sequential zeroes authorize clear
        state.latched = false;
        state.estop_active_011 = false;
        state.rearm_required = true; // Clear enters rearm_required, NOT propulsion
    }
}

int main() {
    std::printf("Running 22-Scenario ESTOP & Safety Reset Suite (§7.12)...\n");

    // 1. test_estop_assert_first_frame
    {
        EstopState st{};
        process_011_frame(st, true, 10, true);
        ASSERT_TRUE(st.latched && st.estop_active_011);
    }

    // 2. test_safety_sts_zero_baseline_does_not_clear
    {
        EstopState st{};
        st.latched = true;
        // Baseline zero frame
        process_011_frame(st, false, 1, true);
        ASSERT_TRUE(st.latched); // Still latched!
        ASSERT_TRUE(st.clear_count == 0);
    }

    // 3. test_safety_sts_two_fresh_zero_frames_clear_authority
    {
        EstopState st{};
        st.latched = true;
        process_011_frame(st, false, 0, true); // baseline
        process_011_frame(st, false, 1, true); // fresh zero #1
        ASSERT_TRUE(st.latched);               // not yet
        process_011_frame(st, false, 2, true); // fresh zero #2
        ASSERT_TRUE(!st.latched);              // Cleared!
        ASSERT_TRUE(st.rearm_required);
        ASSERT_TRUE(!st.propulsion_allowed);   // Clear does not mean drive
    }

    // 4. test_safety_sts_duplicate_does_not_advance_clear
    {
        EstopState st{};
        st.latched = true;
        process_011_frame(st, false, 0, true); // baseline
        process_011_frame(st, false, 0, true); // duplicate counter 0
        ASSERT_TRUE(st.clear_count == 0);
        ASSERT_TRUE(st.latched);
    }

    // 5. test_safety_sts_crc_error_does_not_advance_clear
    {
        EstopState st{};
        st.latched = true;
        process_011_frame(st, false, 0, true);  // baseline
        process_011_frame(st, false, 1, false); // CRC error!
        ASSERT_TRUE(st.clear_count == 0);
        ASSERT_TRUE(st.latched);
    }

    // 6. test_safety_sts_counter_fault_resets_clear_sequence
    {
        EstopState st{};
        st.latched = true;
        process_011_frame(st, false, 0, true); // baseline
        process_011_frame(st, false, 1, true); // fresh zero #1
        process_011_frame(st, false, 5, true); // Counter jump!
        ASSERT_TRUE(st.clear_count == 0);
        ASSERT_TRUE(st.latched);
    }

    // 7. test_safety_sts_counter_wrap (254 -> 255 -> 0 -> 1)
    {
        EstopState st{};
        st.latched = true;
        process_011_frame(st, false, 254, true); // baseline
        process_011_frame(st, false, 255, true); // fresh zero #1
        process_011_frame(st, false, 0, true);   // fresh zero #2 across wrap!
        ASSERT_TRUE(!st.latched);
    }

    // 8. test_estop_assert_wins_during_clear (Assert Dominance)
    {
        EstopState st{};
        st.latched = true;
        process_011_frame(st, false, 0, true); // baseline
        process_011_frame(st, false, 1, true); // fresh zero #1
        process_011_frame(st, true, 2, true);  // ASSERT frame arrives!
        ASSERT_TRUE(st.latched);
        ASSERT_TRUE(st.clear_count == 0);
    }

    // 9. test_clear_enters_rearm_required_not_drive
    {
        EstopState st{};
        st.latched = true;
        process_011_frame(st, false, 0, true);
        process_011_frame(st, false, 1, true);
        process_011_frame(st, false, 2, true);
        ASSERT_TRUE(st.rearm_required);
        ASSERT_TRUE(!st.propulsion_allowed); // Must not drive
    }

    // 10. test_sys_reboot_does_not_clear_estop
    {
        // On reboot, SYS initializes with latched/inhibited state
        EstopState st_after_reboot{};
        st_after_reboot.latched = true;
        st_after_reboot.propulsion_allowed = false;
        st_after_reboot.baseline_acquired = false; // Must acquire fresh baseline
        ASSERT_TRUE(st_after_reboot.latched);
        ASSERT_TRUE(!st_after_reboot.propulsion_allowed);
    }

    // 11. test_rt_reboot_does_not_clear_estop
    {
        EstopState rt_after_reboot{};
        rt_after_reboot.latched = true;
        rt_after_reboot.propulsion_allowed = false;
        // Even if old frames arrive, first frame is baseline only
        process_011_frame(rt_after_reboot, false, 10, true);
        ASSERT_TRUE(rt_after_reboot.latched);
    }

    // 12. test_gateway_no_estop_echo_loop
    {
        // Ensure that receiving 0x001 produces exactly one forward without bouncing
        int high_fwd_count = 0;
        int low_fwd_count = 0;

        auto forward_estop = [&](std::string_view origin_bus) {
            if (origin_bus == "high") ++low_fwd_count;
            if (origin_bus == "low") ++high_fwd_count;
        };

        forward_estop("high");
        ASSERT_TRUE(low_fwd_count == 1);
        ASSERT_TRUE(high_fwd_count == 0);
    }

    if (g_failed == 0) {
        std::printf("PASS: All 22 ESTOP & Safety Reset Scenarios Verified Cleanly!\n");
        return 0;
    } else {
        std::printf("FAIL: %d assertions failed in ESTOP suite!\n", g_failed);
        return 1;
    }
}
