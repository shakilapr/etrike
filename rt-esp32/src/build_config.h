#pragma once
// RT ESP32-S3 — Compile-time feature configuration.
// All feature flags are defined in platformio.ini build_flags.
// This header validates every flag, assigns safe defaults, and exposes
// typed constexpr constants.  Include this header — never read the raw
// macros anywhere else in the codebase.
//
// Flags defined here:
//   ETRIKE_RT_ENCODERS            0 = subsystem disabled   1 = enabled
//   ETRIKE_RT_SPEED_FEEDBACK_SOURCE 0=none  1=MTR  2=RT encoder  3=calculated
//   ETRIKE_RT_PID_MODE            0 = disabled  1 = shadow  2 = active
//   ETRIKE_RT_KINEMATICS_RESOLVER 0 = bicycle physics  1 = direct passthrough
//
// docs/rt-sys-feature-configuration-and-test-plan.md §"Feature configuration"

#include <cstdint>

namespace rt::build {

// ── Safe defaults (all conservative / off) ─────────────────────────
#ifndef ETRIKE_RT_ENCODERS
#  define ETRIKE_RT_ENCODERS 0
#endif
#ifndef ETRIKE_RT_SPEED_FEEDBACK_SOURCE
#  define ETRIKE_RT_SPEED_FEEDBACK_SOURCE 0
#endif
#ifndef ETRIKE_RT_PID_MODE
#  define ETRIKE_RT_PID_MODE 0
#endif
#ifndef ETRIKE_RT_KINEMATICS_RESOLVER
#  define ETRIKE_RT_KINEMATICS_RESOLVER 0
#endif

// ── Typed enumerations ──────────────────────────────────────────────

enum class SpeedFeedbackSource : uint8_t {
    None       = 0,  // open-loop; no feedback dependency
    Mtr        = 1,  // MTR_MOTOR_FBK CAN report (telemetry/supervision only)
    RtEncoder  = 2,  // validated RT PCNT rear-motor encoder
    Calculated = 3,  // estimated from executed commands via plant model
};

enum class PidMode : uint8_t {
    Disabled = 0,  // PID reset; no calculation, no output effect
    Shadow   = 1,  // PID calculates for telemetry only; cannot affect output
    Active   = 2,  // PID correction injected into drive setpoint
};

// ── Typed resolved constants ────────────────────────────────────────
// Use these throughout the codebase — not the raw macros.

constexpr bool kEncodersEnabled =
    (ETRIKE_RT_ENCODERS != 0);

constexpr SpeedFeedbackSource kSpeedFeedbackSource =
    static_cast<SpeedFeedbackSource>(ETRIKE_RT_SPEED_FEEDBACK_SOURCE);

constexpr PidMode kPidMode =
    static_cast<PidMode>(ETRIKE_RT_PID_MODE);

// ── Compile-time validation static_asserts ─────────────────────────
// Any illegal combination causes a hard build error with a readable message.

static_assert(ETRIKE_RT_ENCODERS == 0 || ETRIKE_RT_ENCODERS == 1,
    "ETRIKE_RT_ENCODERS must be 0 (disabled) or 1 (enabled).");

static_assert(ETRIKE_RT_SPEED_FEEDBACK_SOURCE >= 0 &&
              ETRIKE_RT_SPEED_FEEDBACK_SOURCE <= 3,
    "ETRIKE_RT_SPEED_FEEDBACK_SOURCE must be 0=none, 1=MTR, 2=RT encoder, 3=calculated.");

static_assert(ETRIKE_RT_PID_MODE >= 0 && ETRIKE_RT_PID_MODE <= 2,
    "ETRIKE_RT_PID_MODE must be 0=disabled, 1=shadow, 2=active.");

static_assert(ETRIKE_RT_KINEMATICS_RESOLVER == 0 || ETRIKE_RT_KINEMATICS_RESOLVER == 1,
    "ETRIKE_RT_KINEMATICS_RESOLVER must be 0 (bicycle) or 1 (direct passthrough).");

// RT encoder source requires encoder subsystem to be enabled.
static_assert(ETRIKE_RT_SPEED_FEEDBACK_SOURCE != 2 || ETRIKE_RT_ENCODERS == 1,
    "ETRIKE_RT_SPEED_FEEDBACK_SOURCE=2 (RT encoder) requires ETRIKE_RT_ENCODERS=1.");

// PID shadow or active requires a feedback source.
static_assert(ETRIKE_RT_PID_MODE == 0 || ETRIKE_RT_SPEED_FEEDBACK_SOURCE != 0,
    "ETRIKE_RT_PID_MODE shadow/active requires a speed feedback source (!=0).");

// PID active only allowed with a physical or calculated feedback source (>=2).
static_assert(ETRIKE_RT_PID_MODE != 2 || ETRIKE_RT_SPEED_FEEDBACK_SOURCE >= 2,
    "ETRIKE_RT_PID_MODE=2 (active) requires ETRIKE_RT_SPEED_FEEDBACK_SOURCE>=2 "
    "(RT encoder or calculated). MTR report is not accepted for active PID.");

}  // namespace rt::build
