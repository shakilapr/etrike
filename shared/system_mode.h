#pragma once

// -----------------------------------------------------------------------------
// Unified System Run Mode Configuration
// -----------------------------------------------------------------------------
// 0 = PRODUCTION (requires real hardware)
// 1 = HARDWARE BENCH (physical I/O; developer jumper may enable peer bypasses)
// 2 = CAN/SOFTWARE BENCH (missing peers and physical inputs may be simulated)
//
// Each PlatformIO environment must set this explicitly when it is not a
// production build. The safe default prevents a vehicle artifact from
// inheriting simulation bypasses through a shared header.
#ifndef ETRIKE_SYSTEM_RUN_MODE
#define ETRIKE_SYSTEM_RUN_MODE 0
#endif
static_assert(ETRIKE_SYSTEM_RUN_MODE >= 0 && ETRIKE_SYSTEM_RUN_MODE <= 2,
              "ETRIKE_SYSTEM_RUN_MODE must be 0, 1, or 2");
constexpr int SYSTEM_RUN_MODE = ETRIKE_SYSTEM_RUN_MODE;

// The GPIO pin used for Mode 1 hardware override
constexpr int DEVELOPER_OVERRIDE_PIN = 35;

// Runtime bypass flags (initialized based on SYSTEM_RUN_MODE)
extern bool g_bench_solo_mode;
extern bool g_bypass_eps_sync;
extern bool g_bypass_seb_sync;
extern bool g_bypass_mtr_absent;
