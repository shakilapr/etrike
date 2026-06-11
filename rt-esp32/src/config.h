#pragma once
// ESP32-S3-N16R8 — unified configuration constants.
// Single firmware for the 2-node architecture (ESP32-S3 + Jetson Orin NX).
// OPI PSRAM consumes GPIO 33–37 — do not use those pins.

namespace cfg {

// ── CAN (2× SN65HVD230 transceivers) ────────────────────────
constexpr int  kCanBitrateHz            = 500'000;
constexpr int  kCan0TxGpio              =       5;
constexpr int  kCan0RxGpio              =       4;
constexpr int  kCan1TxGpio              =       9;
constexpr int  kCan1RxGpio              =       8;
constexpr bool kSyntreeCanOutputEnabled = false;  // enable after Phase 1 DBC verification

// ── vehicle geometry (mm) ──────────────────────────────────
constexpr float kWheelbaseMM     = 1500.0f;
constexpr float kTrackWidthMM    =  800.0f;
constexpr float kWheelRadiusMM   =  200.0f;

// ── throttle — bidirectional 0–5V ──────────────────────────
constexpr int  kThrottleAdcGpio       =      10;
constexpr int  kThrottleAdcChannel    =       9;   // ADC1_CH9
constexpr int  kThrottleDacI2cAddr    =    0x60;   // MCP4725, 12-bit, 0–5V on I2C0
constexpr unsigned kThrottleDeadZone  =     200;
constexpr int  kThrottleMaxSpeedMmps  =    3000;

// ── gear selector — 72V discrete I/O ───────────────────────
// Inputs: TLP281 optoisolator, active-low (72V present → GPIO LOW)
// Outputs: GPIO → transistor → relay → 72V line
constexpr int  kGearDGpioIn    =      13;
constexpr int  kGearSGpioIn    =      26;
constexpr int  kGearRGpioIn    =      14;
constexpr int  kGearDGpioOut   =       6;
constexpr int  kGearSGpioOut   =      42;
constexpr int  kGearRGpioOut   =      43;

// ── steering (Syntree EPS-C via CAN) ───────────────────────
constexpr float kSteerLimitDeg       =  45.0f;
constexpr float kSteerSlewRateDegS   = 180.0f;

// ── brake (Syntree SEB via CAN) ────────────────────────────
// No GPIO — brake is CAN-controlled.

// ── safety inputs ──────────────────────────────────────────
constexpr int  kEstopGpio         =       1;
constexpr int  kBrakeLeverGpio    =       2;

// ── mode switch ────────────────────────────────────────────
constexpr int  kModeSwitchGpio    =      11;

// ── lighting — 12V via transistor ──────────────────────────
constexpr int  kSignalLeftGpio     =      40;
constexpr int  kSignalRightGpio    =      41;
constexpr int  kModeAutoLightGpio  =      38;
constexpr int  kModeManualLightGpio=      39;

// ── 12V PSU ────────────────────────────────────────────────
constexpr int  k12vPsuGpio         =      44;

// ── speed limits ───────────────────────────────────────────
constexpr int  kMaxSpeedFwdMmps    =  3000;
constexpr int  kMaxSpeedRevMmps    =   500;
constexpr int  kLowSpeedThreshMmps =    50;

// ── PID ────────────────────────────────────────────────────
constexpr float kPidKp           =  1.0f;
constexpr float kPidKi           =  0.1f;
constexpr float kPidKd           =  0.05f;
constexpr float kPidMaxIntegral  = 500.0f;

// ── obstacle ───────────────────────────────────────────────
constexpr unsigned kObstacleStopDistMM  =   300;
constexpr unsigned kObstacleClearDistMM =  3000;
constexpr int      kObstacleTrigGpio    =    15;
constexpr int      kObstacleEchoGpio    =    16;

// ── encoder ────────────────────────────────────────────────
constexpr int  kEncoderAGpio       =       3;
constexpr int  kEncoderBGpio       =      17;

// ── IMU (I2C) ──────────────────────────────────────────────
constexpr int  kImuSdaGpio         =      19;
constexpr int  kImuSclGpio         =      20;

// ── external watchdog ──────────────────────────────────────
constexpr int  kExtWatchdogGpio    =      21;

// ── timing ─────────────────────────────────────────────────
constexpr int  kControlLoopHz       = 100;
constexpr int  kHeartbeatIntervalMs = 500;
constexpr int  kJetsonHbTimeoutMs   = 1500;
constexpr int  kCmdStaleTimeoutMs   =  200;
constexpr int  kSafetyCheckHz       =   20;

}  // namespace cfg
