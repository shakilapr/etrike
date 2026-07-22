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

// Mode 1 hardware override: active-low jumper to GND. GPIO42 is exposed on
// the ESP32-S3-DevKitC-1, is not a strapping pin, and is outside the N16R8
// octal-memory GPIO33-37 range. It overlaps the external JTAG MTMS signal.
constexpr int DEVELOPER_OVERRIDE_PIN = 42;

// Runtime bypass flags (initialized based on SYSTEM_RUN_MODE)
extern bool g_bench_solo_mode;
extern bool g_bypass_eps_sync;
extern bool g_bypass_seb_sync;
extern bool g_bypass_mtr_absent;
