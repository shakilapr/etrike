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

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_sys_qa_brake_following_error_and_0x721_corruption);
    RUN_TEST(test_sys_qa_estop_rx_rate_limiting_bypass);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
