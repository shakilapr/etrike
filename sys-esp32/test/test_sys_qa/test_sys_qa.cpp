#include <unity.h>
#include <cstdint>
#include "protocol/compat/can.hpp"
#include "shared_config.h"
#include "config.h"

void setUp(void) {}
void tearDown(void) {}

void test_sys_qa_brake_following_error_and_0x721_corruption(void) {
    uint16_t g_cmd_stroke_raw = 0;
    int32_t brake_kpa = 2000;
    
    can::custom::seb::Command out{};
    if (brake_kpa > 0) {
        out.control_mode = can::custom::seb::ControlMode::Pressure;
        uint16_t kStrokeRawZero = static_cast<uint16_t>((0.0f - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale);
        out.stroke_request_raw = kStrokeRawZero; // 600
        g_cmd_stroke_raw = out.stroke_request_raw;
    }
    
    can::Frame frame = can::Frame::standard(can::kIdSebStatus, 8);
    frame.data = {0x05, 0x00, 0x20, 0x28, 0x00, 0x00, 0x00, 0xF2};
    can::custom::seb::Status status{};
    TEST_ASSERT_EQUAL_UINT8(uint8_t(can::gen::CodecStatus::Ok),
                            uint8_t(can::custom::seb::decode_status(frame.view(), status)));
    TEST_ASSERT_EQUAL_UINT8(1, status.control_mode);
    uint16_t actual_raw = status.stroke_value_raw;
    
    uint16_t cmd = g_cmd_stroke_raw;
    uint16_t diff = (cmd > actual_raw) ? (cmd - actual_raw) : (actual_raw - cmd);
    
    TEST_ASSERT_TRUE(diff > sys::kBrakeFollowingErrRaw);
}

void test_sys_qa_estop_rx_rate_limiting_bypass(void) {
    int estop_rx_count = 0;
    int accepted_estops = 0;
    int dropped_estops = 0;
    
    for (int i=0; i<3; i++) {
        if (++estop_rx_count <= sys::kEstopRateLimitMax) {
            accepted_estops++;
        } else {
            dropped_estops++;
        }
    }
    
    TEST_ASSERT_TRUE(dropped_estops > 0);
}

#include "mtr_estop_ack.h"

void test_sys_mtr_estop_ack_watchdog(void) {
    sys::MtrEstopAckWatchdog wd;
    TEST_ASSERT_FALSE(wd.is_pending());

    // 1. Trigger ESTOP when MTR has not acknowledged
    wd.trigger(1000, 0);
    TEST_ASSERT_TRUE(wd.is_pending());
    TEST_ASSERT_EQUAL_UINT8(sys::kMtrEstopAckMaxRetries, wd.retries_left());
    TEST_ASSERT_EQUAL_UINT32(1100, wd.deadline());

    // 2. Before deadline, check returns None and stays pending
    TEST_ASSERT_EQUAL(int(sys::MtrEstopAckWatchdog::Action::None), int(wd.check_tick(1050, 0)));
    TEST_ASSERT_TRUE(wd.is_pending());

    // 3. At first deadline (1100ms), ACK still missing -> Action::Retry, retries decrements to 2
    TEST_ASSERT_EQUAL(int(sys::MtrEstopAckWatchdog::Action::Retry), int(wd.check_tick(1100, 0)));
    TEST_ASSERT_TRUE(wd.is_pending());
    TEST_ASSERT_EQUAL_UINT8(2, wd.retries_left());
    TEST_ASSERT_EQUAL_UINT32(1200, wd.deadline());

    // 4. Second deadline (1200ms) -> Action::Retry, retries decrements to 1
    TEST_ASSERT_EQUAL(int(sys::MtrEstopAckWatchdog::Action::Retry), int(wd.check_tick(1200, 0)));
    TEST_ASSERT_TRUE(wd.is_pending());
    TEST_ASSERT_EQUAL_UINT8(1, wd.retries_left());

    // 5. Third deadline (1300ms) -> Action::Retry, retries decrements to 0
    TEST_ASSERT_EQUAL(int(sys::MtrEstopAckWatchdog::Action::Retry), int(wd.check_tick(1300, 0)));
    TEST_ASSERT_TRUE(wd.is_pending());
    TEST_ASSERT_EQUAL_UINT8(0, wd.retries_left());

    // 6. Fourth deadline (1400ms) -> Action::ExhaustedFault, latched_fault set, pending cleared
    TEST_ASSERT_EQUAL(int(sys::MtrEstopAckWatchdog::Action::ExhaustedFault), int(wd.check_tick(1400, 0)));
    TEST_ASSERT_FALSE(wd.is_pending());
    TEST_ASSERT_TRUE(wd.has_latched_fault());
    TEST_ASSERT_FALSE(wd.has_acknowledged());

    // 7. Early clear scenario: ACK arrives before deadline
    wd.reset();
    TEST_ASSERT_FALSE(wd.has_acknowledged());
    wd.trigger(2000, 0);
    TEST_ASSERT_TRUE(wd.is_pending());
    TEST_ASSERT_FALSE(wd.has_acknowledged());
    // 0x206 arrives with ESTOP_ACTIVE at 2020ms
    wd.on_feedback_received(shared::kMtrFaultEstopActive);
    TEST_ASSERT_FALSE(wd.is_pending());
    TEST_ASSERT_FALSE(wd.has_latched_fault());
    TEST_ASSERT_TRUE(wd.has_acknowledged());
    TEST_ASSERT_EQUAL(int(sys::MtrEstopAckWatchdog::Action::None), int(wd.check_tick(2100, 0)));
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_sys_qa_brake_following_error_and_0x721_corruption);
    RUN_TEST(test_sys_qa_estop_rx_rate_limiting_bypass);
    RUN_TEST(test_sys_mtr_estop_ack_watchdog);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
