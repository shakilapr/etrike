#pragma once
// PWT ESP32-S3 — standalone powertrain-node configuration constants.
// See pwt-esp32/pwt-architecture.md.

#include <cstdint>
#include "generated/pwt_can_data.h"

namespace pwt {

#ifndef PWT_DCDC_DEFAULT_ENABLED
#define PWT_DCDC_DEFAULT_ENABLED 1
#endif
static_assert(PWT_DCDC_DEFAULT_ENABLED == 0 || PWT_DCDC_DEFAULT_ENABLED == 1,
              "PWT_DCDC_DEFAULT_ENABLED must be 0 or 1");
constexpr bool kDcdcDefaultEnabled = PWT_DCDC_DEFAULT_ENABLED != 0;

// ── Powertrain CAN (the ESP32-S3's only TWAI controller, 250 kbit/s) ─
constexpr int kCanPwtBitrateHz = can_data::kBitrateHz;
constexpr int kCanPwtTxGpio    = 7;
constexpr int kCanPwtRxGpio    = 6;

// ── DC-DC Converter ─────────────────────────────────────────────────
// Manufacturer protocol: VCU(27H) → DCDC(2BH), 100 ms cycle.
// Extended CAN ID 0x10262B27, DLC=8.
// Byte 0: Control   (00=Disable, 01=Enable)
// Byte 7: Reset Ctrl (00=No reset, 01=Reset)
// Bytes 1-6: Reserved (0xFF)
constexpr uint32_t kDcdcCmdId      = can_data::kIdDcdcCmd;
constexpr int      kDcdcCycleMs    = can_data::kDcdcCmdCycleMs;
constexpr uint8_t  kDcdcEnable     = can_data::kDcdcEnable;
constexpr uint8_t  kDcdcDisable    = can_data::kDcdcDisable;
constexpr uint8_t  kDcdcNoReset    = can_data::kDcdcNoReset;
constexpr uint8_t  kDcdcReset      = can_data::kDcdcReset;
constexpr uint8_t  kDcdcReserved   = can_data::kDcdcReserved;

// ── External Watchdog ───────────────────────────────────────────────
constexpr int kWdtToggleGpio  = 21;
constexpr int kWdtToggleRateHz = 20;

// ── Timing ──────────────────────────────────────────────────────────
constexpr int kControlLoopHz  = 100;   // base tick
constexpr int kDcdcTaskRateHz = 10;    // = 1000 / kDcdcCycleMs

} // namespace pwt
