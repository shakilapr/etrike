#pragma once
// PWT ESP32-S3 — Powertrain CAN Gateway configuration constants.
// Standalone unit — does not depend on shared/.
// See pwt-esp32/pwt-architecture.md.

#include <cstdint>

namespace pwt {

// ── Powertrain CAN (TWAI1, 250 kbit/s) ─────────────────────────────
constexpr int kCanPwtBitrateHz = 250'000;
constexpr int kCanPwtTxGpio    = 7;
constexpr int kCanPwtRxGpio    = 6;

// ── DC-DC Converter ─────────────────────────────────────────────────
// Manufacturer protocol: VCU(27H) → DCDC(2BH), 100 ms cycle.
// Extended CAN ID 0x10262B27, DLC=8.
// Byte 0: Control   (00=Disable, 01=Enable)
// Byte 7: Reset Ctrl (00=No reset, 01=Reset)
// Bytes 1-6: Reserved (0xFF)
constexpr uint32_t kDcdcCmdId      = 0x10262B27;  // 29-bit extended
constexpr int      kDcdcCycleMs    = 100;          // 10 Hz
constexpr uint8_t  kDcdcEnable     = 0x01;
constexpr uint8_t  kDcdcDisable    = 0x00;
constexpr uint8_t  kDcdcNoReset    = 0x00;
constexpr uint8_t  kDcdcReset      = 0x01;
constexpr uint8_t  kDcdcReserved   = 0xFF;        // bytes 1-6

// ── External Watchdog ───────────────────────────────────────────────
constexpr int kWdtToggleGpio  = 21;
constexpr int kWdtToggleRateHz = 20;

// ── Timing ──────────────────────────────────────────────────────────
constexpr int kControlLoopHz  = 100;   // base tick
constexpr int kDcdcTaskRateHz = 10;    // = 1000 / kDcdcCycleMs

} // namespace pwt
