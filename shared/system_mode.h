#pragma once

// -----------------------------------------------------------------------------
// Unified System Run Mode Configuration
// -----------------------------------------------------------------------------
// 0 = PRODUCTION (Strict safety, requires real hardware)
// 1 = PROTOTYPE HARDWARE (Checks physical developer jumper pin to allow bypasses)
// 2 = PURE SOFTWARE SIM (Mocks everything, no hardware required)
constexpr int SYSTEM_RUN_MODE = 2;

// The GPIO pin used for Mode 1 hardware override
constexpr int DEVELOPER_OVERRIDE_PIN = 35;

// Runtime bypass flags (initialized based on SYSTEM_RUN_MODE)
extern bool g_bench_solo_mode;
extern bool g_bypass_eps_sync;
extern bool g_bypass_seb_sync;
extern bool g_bypass_mtr_absent;
