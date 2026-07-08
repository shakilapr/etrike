#include <unity.h>

#include "../../src/physics_model.cpp"

void test_forward_straight_resolves_zero_steer() {
    rt::PhysicsModel pm;
    rt::DriveCmd cmd{};
    cmd.speed_mmps = 2000;
    cmd.yaw_rate_mrad_s = 0;

    rt::ResolvedSetpoint out{};
    TEST_ASSERT_TRUE(pm.resolve(cmd, out));
    TEST_ASSERT_INT_WITHIN(10, 0, out.steer_angle_mdeg);
    TEST_ASSERT_EQUAL_INT32(2000, out.motor_speed_mmps);
}

void test_reverse_turn_has_negative_steer_for_positive_yaw() {
    rt::PhysicsModel pm;
    rt::DriveCmd cmd{};
    cmd.speed_mmps = -1000;
    cmd.yaw_rate_mrad_s = 100;

    rt::ResolvedSetpoint out{};
    TEST_ASSERT_TRUE(pm.resolve(cmd, out));
    TEST_ASSERT_TRUE(out.reversing);
    TEST_ASSERT_LESS_THAN_INT32(0, out.steer_angle_mdeg);
}

void test_zero_speed_yaw_does_not_lurch() {
    rt::PhysicsModel pm;
    rt::DriveCmd cmd{};
    cmd.speed_mmps = 0;
    cmd.yaw_rate_mrad_s = 100;

    rt::ResolvedSetpoint out{};
    TEST_ASSERT_TRUE(pm.resolve(cmd, out));
    TEST_ASSERT_EQUAL_INT32(0, out.motor_speed_mmps);
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_forward_straight_resolves_zero_steer);
    RUN_TEST(test_reverse_turn_has_negative_steer_for_positive_yaw);
    RUN_TEST(test_zero_speed_yaw_does_not_lurch);
    return UNITY_END();
}
