#include <unity.h>
#include <cmath>
#include <algorithm>
#include "physics_model.h"
#include "protocol/compat/can.hpp"

using namespace rt;

// High-level (HOST_DRIVE_CMD 0x300) -> RT resolver -> RT_DRIVE_CMD (0x204) setpoint.
// This verifies the "tuktuk math": a high-level (speed_mmps, yaw_rate_mrad_s)
// command resolves to the correct motor speed and steering angle via the
// inverse-bicycle model (L = kWheelbaseMM = 1.5 m, steer soft limit 40 deg).
// steer = atan(L * w / v); motor = clamp(v, -0.5, 3.0) * 1000.

static void resolve_host_cmd(int32_t speed_mmps, int32_t yaw_mrad_s,
                             ResolvedSetpoint& out) {
    can::gen::HostDriveCmd host{};
    host.speed_mmps = speed_mmps;
    host.yaw_rate_mrad_s = yaw_mrad_s;
    host.gear = 0;
    can::Frame f;
    TEST_ASSERT_TRUE(etrike::protocol::succeeded(can::gen::encode_host_drive_cmd(host, f)));

    can::gen::HostDriveCmd decoded{};
    TEST_ASSERT_TRUE(etrike::protocol::succeeded(can::gen::decode_host_drive_cmd(f.view(), decoded)));
    TEST_ASSERT_EQUAL(speed_mmps, decoded.speed_mmps);
    TEST_ASSERT_EQUAL(yaw_mrad_s, decoded.yaw_rate_mrad_s);

    DriveCmd cmd{decoded.speed_mmps, decoded.yaw_rate_mrad_s};
    PhysicsModel model;
    TEST_ASSERT_TRUE(model.resolve(cmd, out));
}

// Resolve directly at the RT setpoint stage (bypasses HOST 0x300 range clamp),
// used to exercise the resolver's own internal speed clamp for out-of-range inputs.
static void resolve_direct(int32_t speed_mmps, int32_t yaw_mrad_s,
                           ResolvedSetpoint& out) {
    PhysicsModel model;
    DriveCmd cmd{speed_mmps, yaw_mrad_s};
    TEST_ASSERT_TRUE(model.resolve(cmd, out));
}

void setUp(void) {}
void tearDown(void) {}

void test_rt_resolver_straight() {
    ResolvedSetpoint o;
    resolve_host_cmd(1500, 0, o);          // 1.5 m/s straight
    TEST_ASSERT_EQUAL(1500, o.motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, 0, o.steer_angle_mdeg);
    TEST_ASSERT_FALSE(o.reversing);
}

void test_rt_resolver_forward_left_and_right() {
    ResolvedSetpoint o;
    resolve_host_cmd(2000, 100, o);        // +yaw -> right(+) steer
    TEST_ASSERT_EQUAL(2000, o.motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, 4288, o.steer_angle_mdeg);   // atan(1.5*0.1/2.0)
    TEST_ASSERT_FALSE(o.reversing);

    resolve_host_cmd(2000, -100, o);       // -yaw -> left(-) steer
    TEST_ASSERT_EQUAL(2000, o.motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, -4288, o.steer_angle_mdeg);
}

void test_rt_resolver_reverse_sign() {
    ResolvedSetpoint o;
    resolve_host_cmd(-300, 50, o);         // reverse + yaw -> left(-) steer
    TEST_ASSERT_EQUAL(-300, o.motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, -14036, o.steer_angle_mdeg); // atan(1.5*0.05/-0.3)
    TEST_ASSERT_TRUE(o.reversing);

    resolve_host_cmd(-300, -50, o);        // reverse - yaw -> right(+) steer
    TEST_ASSERT_EQUAL(-300, o.motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, 14036, o.steer_angle_mdeg);
    TEST_ASSERT_TRUE(o.reversing);
}

void test_rt_resolver_low_speed() {
    ResolvedSetpoint o;
    resolve_host_cmd(30, 0, o);            // below low-speed threshold, no yaw -> decay (speed kept)
    TEST_ASSERT_EQUAL(30, o.motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, 0, o.steer_angle_mdeg);

    resolve_host_cmd(30, 200, o);          // low speed + yaw -> full lock steer, speed kept
    TEST_ASSERT_EQUAL(30, o.motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, 40000, o.steer_angle_mdeg);
    TEST_ASSERT_FALSE(o.reversing);
}

void test_rt_resolver_speed_clamps() {
    ResolvedSetpoint o;
    resolve_host_cmd(3000, 0, o);          // max forward (HOST range boundary)
    TEST_ASSERT_EQUAL(3000, o.motor_speed_mmps);

    resolve_direct(4000, 0, o);            // overspeed forward -> resolver clamps to 3000
    TEST_ASSERT_EQUAL(3000, o.motor_speed_mmps);

    resolve_host_cmd(-500, 0, o);          // max reverse (HOST range boundary)
    TEST_ASSERT_EQUAL(-500, o.motor_speed_mmps);
    TEST_ASSERT_TRUE(o.reversing);

    resolve_direct(-600, 0, o);            // overspeed reverse -> resolver clamps to -500
    TEST_ASSERT_EQUAL(-500, o.motor_speed_mmps);
    TEST_ASSERT_TRUE(o.reversing);
}

void test_rt_resolver_steering_saturation() {
    ResolvedSetpoint o;
    resolve_host_cmd(2000, 3000, o);       // huge yaw -> steer saturates at 40 deg
    TEST_ASSERT_EQUAL(2000, o.motor_speed_mmps);
    TEST_ASSERT_INT_WITHIN(10, 40000, o.steer_angle_mdeg);
    TEST_ASSERT_TRUE(o.steer_saturated);
    TEST_ASSERT_FALSE(o.steer_valid);
}

extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_rt_resolver_straight);
    RUN_TEST(test_rt_resolver_forward_left_and_right);
    RUN_TEST(test_rt_resolver_reverse_sign);
    RUN_TEST(test_rt_resolver_low_speed);
    RUN_TEST(test_rt_resolver_speed_clamps);
    RUN_TEST(test_rt_resolver_steering_saturation);
    UNITY_END();
}

#if defined(HOST_BUILD) || defined(NATIVE_TEST_ENV) || !defined(ESP_PLATFORM)
int main(int argc, char **argv) {
    app_main();
    return 0;
}
#endif
