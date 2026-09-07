// N2 — cross-node ESTOP consistency (SYS-publishing half).
//
// RT only releases its own ESTOP latch after two consecutive fresh
// SYS_SAFETY_STS (0x011) frames with estop_active == 0 (asymmetric clear in
// rt-esp32 can_dispatch.h). The precondition is that SYS must NEVER broadcast
// estop_active == 0 while it is still estopped — otherwise RT could get a
// false "all-clear" and drive. This test pins that contract on the SYS side:
// the estop_active bit SYS publishes in 0x011 / 0x7FE must track its latch.

#include <unity.h>
#include <cstdint>
#include "shared_config.h"
#include "safety_monitor.h"
#include "protocol/compat/can.hpp"

using namespace sys;

void setUp(void) {}
void tearDown(void) {}

static void assert_estop_bit(uint32_t id, bool latched, bool expect_bit) {
    SafetyMonitor sm;
    sm.init();
    sm.set_estop(latched);

    can::Frame fr;
    bool encoded = false;
    if (id == can::kIdSysSafetySts) {
        can::gen::SysSafetySts s{};
        s.estop_active = sm.estop_active();
        encoded = (can::gen::encode_sys_safety_sts(s, fr) == can::gen::CodecStatus::Ok);
        TEST_ASSERT_TRUE(encoded);
        can::gen::SysSafetySts d{};
        TEST_ASSERT_EQUAL(can::gen::CodecStatus::Ok,
                          can::gen::decode_sys_safety_sts(fr.view(), d));
        TEST_ASSERT_EQUAL(expect_bit ? 1 : 0, d.estop_active);
    } else {  // SYS_HEARTBEAT 0x7FE (RT also reads estop_active here)
        can::gen::SysHeartbeat s{};
        s.estop_active = sm.estop_active();
        encoded = (can::gen::encode_sys_heartbeat(s, fr) == can::gen::CodecStatus::Ok);
        TEST_ASSERT_TRUE(encoded);
        can::gen::SysHeartbeat d{};
        TEST_ASSERT_EQUAL(can::gen::CodecStatus::Ok,
                          can::gen::decode_sys_heartbeat(fr.view(), d));
        TEST_ASSERT_EQUAL(expect_bit ? 1 : 0, d.estop_active);
    }
}

static void test_sys_safety_sts_estop_bit_follows_latch(void) {
    // While latched, RT must keep its own latch (bit == 1).
    assert_estop_bit(can::kIdSysSafetySts, /*latched=*/true,  /*expect=*/true);
    // After a validated REARM, RT may begin its two-frame clear (bit == 0).
    assert_estop_bit(can::kIdSysSafetySts, /*latched=*/false, /*expect=*/false);
}

static void test_sys_heartbeat_estop_bit_follows_latch(void) {
    assert_estop_bit(can::kIdSysHeartbeat, /*latched=*/true,  /*expect=*/true);
    assert_estop_bit(can::kIdSysHeartbeat, /*latched=*/false, /*expect=*/false);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_sys_safety_sts_estop_bit_follows_latch);
    RUN_TEST(test_sys_heartbeat_estop_bit_follows_latch);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
