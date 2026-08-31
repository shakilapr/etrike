// Liveness + safety supervision unit tests — pure, deterministic.

#include <cstdio>
#include <cstdlib>

#include "domain/liveness.h"
#include "domain/safety.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using rta::LivenessMonitor;
using rta::FreshnessMonitor;
using rta::SafetySupervisor;
using rta::SafetyResult;
using rta::Mode;
using rta::MotorFeedback;
using rta::DriveCommand;

// LivenessMonitor: frozen-counter detection.
void test_liveness_frozen_counter() {
    LivenessMonitor lm;
    constexpr rta::TimeUs kTimeout = 300'000;  // 300 ms

    // Advancing counter -> alive.
    CHECK(!lm.observe(0, 5, kTimeout));
    CHECK(lm.alive());
    CHECK(!lm.observe(100'000, 5, kTimeout));  // unchanged, within timeout
    CHECK(lm.alive());
    CHECK(!lm.observe(200'000, 5, kTimeout));
    CHECK(lm.alive());

    // Counter freezes beyond timeout -> lost.
    CHECK(!lm.observe(301'000, 5, kTimeout));
    CHECK(!lm.alive());

    // Counter advances again -> recovered (observe returns was_lost=true).
    bool was_lost = lm.observe(400'000, 6, kTimeout);
    CHECK(was_lost);
    CHECK(lm.alive());
}

// FreshnessMonitor: never seen / stale transitions.
void test_freshness() {
    FreshnessMonitor fm;
    constexpr rta::TimeUs kTimeout = 200'000;
    CHECK(fm.stale(0, kTimeout));        // never seen -> stale
    fm.observe(0);
    CHECK(!fm.stale(100'000, kTimeout));
    CHECK(!fm.stale(199'000, kTimeout));
    CHECK(fm.stale(201'000, kTimeout));
    fm.observe(250'000);
    CHECK(!fm.stale(260'000, kTimeout));
}

// Safety: ESTOP latch forces zero setpoints + max brake.
void test_safety_estop() {
    SafetySupervisor ss;
    MotorFeedback mf;
    DriveCommand dc;
    rta::SteeringFeedback sf;
    sf.valid = true;

    SafetyResult r = ss.evaluate(0, false, true, Mode::Auto, mf, dc, sf, 0, true,
                                 3000, true, true);
    CHECK(r.zero_setpoints);
    CHECK(r.brake_kpa == 5000);  // shared::kMaxBrakeKpa
    CHECK(r.disable_steering);
    CHECK(r.estop_reason == rta::kEstopReasonCanEstop);

    // Mode ESTOP also zeroes.
    r = ss.evaluate(0, false, false, Mode::Estop, mf, dc, sf, 0, true,
                    3000, true, true);
    CHECK(r.zero_setpoints);
    CHECK(r.estop_reason == rta::kEstopReasonButton);
}

// Safety: host heartbeat loss -> assisted stop (2000 kPa), not full ESTOP.
void test_safety_host_loss() {
    SafetySupervisor ss;
    MotorFeedback mf;
    DriveCommand dc;
    rta::SteeringFeedback sf;

    SafetyResult r = ss.evaluate(0, false, false, Mode::Auto, mf, dc, sf, 0, true,
                                 3000, false /*host lost*/, true);
    CHECK(r.zero_setpoints);
    CHECK(r.brake_kpa >= 2000);  // assisted stop
    CHECK(!r.disable_steering);  // not a full ESTOP
    CHECK(ss.host_lost());
}

// Safety: MTR feedback loss zeroes setpoints.
void test_safety_mtr_loss() {
    SafetySupervisor ss;
    MotorFeedback mf;
    DriveCommand dc;
    rta::SteeringFeedback sf;

    SafetyResult r = ss.evaluate(0, false, false, Mode::Auto, mf, dc, sf, 0, true,
                                 3000, true, false /*mtr lost*/);
    CHECK(r.zero_setpoints);
    CHECK(ss.mtr_lost());
}

// Safety: steering follow-error persists > 300 ms -> ESTOP.
void test_safety_follow_error() {
    SafetySupervisor ss;
    MotorFeedback mf;
    mf.actual_speed_mmps = 2000;
    DriveCommand dc;
    rta::SteeringFeedback sf;
    sf.valid = true;
    sf.angle_0_1deg = 0;       // actual 0
    const std::int16_t kCmd = 500;  // commanded 50° (far beyond threshold)

    // At 2 m/s, dynamic limit ~40° => threshold ~10°; err 50° exceeds.
    SafetyResult r = ss.evaluate(0, false, false, Mode::Auto, mf, dc, sf, kCmd, true,
                                 3000, true, true);
    CHECK(!r.zero_setpoints);  // not yet persisted
    // Advance 400 ms (persist window 300 ms).
    r = ss.evaluate(400'000, false, false, Mode::Auto, mf, dc, sf, kCmd, true,
                    3000, true, true);
    CHECK(r.zero_setpoints);
    CHECK(r.estop_reason == rta::kEstopReasonFollowingError);
}

// Safety: obstacle within stop distance at speed -> obstacle ESTOP.
void test_safety_obstacle() {
    SafetySupervisor ss;
    MotorFeedback mf;
    mf.actual_speed_mmps = 1000;
    DriveCommand dc;
    rta::SteeringFeedback sf;

    SafetyResult r = ss.evaluate(0, false, false, Mode::Auto, mf, dc, sf, 0, true,
                                 200 /*< stop 300*/, true, true);
    CHECK(r.obstacle_triggered);
    CHECK(r.disable_steering);
    CHECK(r.estop_reason == rta::kEstopReasonObstacle);
}

// Safety: startup grace suppresses non-ESTOP checks.
void test_safety_startup_grace() {
    SafetySupervisor ss;
    MotorFeedback mf;
    mf.actual_speed_mmps = 2000;
    DriveCommand dc;
    rta::SteeringFeedback sf;
    sf.valid = true;

    // Host lost + MTR lost during grace -> only liveness zero (no follow/obstacle).
    SafetyResult r = ss.evaluate(0, true, false, Mode::Auto, mf, dc, sf, 500, true,
                                 200, false, false);
    CHECK(r.zero_setpoints);  // liveness still zeroes (safety-critical)
}

}  // namespace

int main() {
    test_liveness_frozen_counter();
    test_freshness();
    test_safety_estop();
    test_safety_host_loss();
    test_safety_mtr_loss();
    test_safety_follow_error();
    test_safety_obstacle();
    test_safety_startup_grace();
    if (g_failures) {
        std::printf("safety: %d FAILURES\n", g_failures);
        return 1;
    }
    std::printf("safety: all tests passed\n");
    return 0;
}
