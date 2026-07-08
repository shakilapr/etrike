#include <unity.h>
#include "physics_model.h"
#include "seb_request.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_bug_4_4_reverse_steer_inversion(void) {
    using namespace rt;
    PhysicsModel phys;
    DriveCmd fwd_cmd { 1000, 500 };
    ResolvedSetpoint fwd_out;
    phys.resolve(fwd_cmd, fwd_out);
    
    DriveCmd rev_cmd { -1000, 500 }; 
    ResolvedSetpoint rev_out;
    phys.resolve(rev_cmd, rev_out);

    TEST_ASSERT_LESS_THAN(0, rev_out.steer_angle_mdeg);
}

void test_bug_4_5_spontaneous_forward_lurch(void) {
    using namespace rt;
    PhysicsModel phys;
    DriveCmd zero_cmd { 0, 500 };
    ResolvedSetpoint zero_out;
    phys.resolve(zero_cmd, zero_out);

    TEST_ASSERT_EQUAL(0, zero_out.motor_speed_mmps);
}

void test_bug_4_10_seb_alignment_bit_uninitialized(void) {
    can::VcuSebReq auto_req = rt::make_seb_auto_req(2000);
    TEST_ASSERT_EQUAL(1, auto_req.align_enable);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_bug_4_4_reverse_steer_inversion);
    RUN_TEST(test_bug_4_5_spontaneous_forward_lurch);
    RUN_TEST(test_bug_4_10_seb_alignment_bit_uninitialized);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
