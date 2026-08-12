#include <cstdio>

#include "phase2_motion.h"

static int pass = 0;
static int fail = 0;

#define CHECK(cond) do { \
    if (cond) { ++pass; } \
    else { ++fail; std::fprintf(stderr, "FAIL %s:%d\n", __FILE__, __LINE__); } \
} while (0)

int main() {
    std::printf("\n=== RT Phase 2 Motion ===\n\n");

    can::gen::HostSteerCmd direct{125, true, 0, 1};
    rt::ResolvedSetpoint setpoint{};
    setpoint.steer_angle_mdeg = -3000;
    CHECK(rt::apply_fresh_direct_steering(direct, 1'000'000, 1'050'000, setpoint));
    CHECK(setpoint.steer_angle_mdeg == 12500);
    CHECK(setpoint.steer_valid);

    setpoint.steer_angle_mdeg = -3000;
    CHECK(!rt::apply_fresh_direct_steering(
        direct, 1'000'000,
        1'000'000 + int64_t(rt::kDirectSteerTimeoutMs) * 1000 + 1,
        setpoint));
    CHECK(setpoint.steer_angle_mdeg == -3000);

    direct.angle_valid = false;
    CHECK(!rt::apply_fresh_direct_steering(direct, 1'000'000, 1'000'001, setpoint));

    CHECK(rt::estimate_yaw_rate_mrad_s(1500, 450) == 1000);
    CHECK(rt::estimate_yaw_rate_mrad_s(-1500, 450) == -1000);
    CHECK(rt::estimate_yaw_rate_mrad_s(0, 450) == 0);

    const int64_t now = 2'000'000;
    auto report = rt::make_motion_report(
        now, 1500, uint8_t(can::Gear::D), now - 50'000,
        450, 1, now - 50'000, 42);
    CHECK(report.speed_mmps == 1500);
    CHECK(report.yaw_rate_mrad_s == 1000);
    CHECK(report.gear == uint8_t(can::Gear::D));
    CHECK(report.speed_valid && report.yaw_rate_valid && report.gear_valid);
    CHECK(report.rolling_counter == 42);

    report = rt::make_motion_report(
        now, 1500, uint8_t(can::Gear::D),
        now - int64_t(rt::kMotionFeedbackTimeoutMs) * 1000 - 1,
        450, 1, now - 50'000, 43);
    CHECK(!report.speed_valid && !report.yaw_rate_valid && !report.gear_valid);
    CHECK(report.yaw_rate_mrad_s == 0);

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}
