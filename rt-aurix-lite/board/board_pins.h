#pragma once
// Board pin definitions for the AURIX Lite Kit V2 (KIT_A2G_TC375_LITE,
// SAK-TC375TP-96F300W AA, LQFP-176).
//
// This is the TARGET board pin map. The portable firmware (src/) does not
// reference pins; the board HAL (board/hal_aurix) maps rta::hal logical
// signals to these. Values are verified against aurix.md (board manual)
// and the iLLD pin maps.

#include <cstdint>
#include "IfxPort_reg.h"
#include "IfxCan_PinMap_TC37x_LQFP176.h"

namespace rta::board {

// ── CAN (frozen from iLLD IfxCan_PinMap_TC37x_LQFP176) ──────────────
// CAN_LOW  : CAN0 Node 0  — on-board TLE9251VSJ
// CAN_HIGH : CAN0 Node 2  — external transceiver on mikroBUS 13/14
inline constexpr IfxCan_Txd_Out* kCanLowTx  = &IfxCan_TXD00_P20_8_OUT;
inline constexpr IfxCan_Rxd_In*  kCanLowRx  = &IfxCan_RXD00B_P20_7_IN;
inline constexpr IfxCan_Txd_Out* kCanHighTx = &IfxCan_TXD02_P15_0_OUT;
inline constexpr IfxCan_Rxd_In*  kCanHighRx = &IfxCan_RXD02A_P15_1_IN;

// CAN_LOW standby control (drive LOW to enable normal mode).
inline constexpr IfxPort_Pin kCanLowStbPort = IfxPort_Index_20;
inline constexpr std::uint8_t kCanLowStbPin = 6;  // P20.6 CAN_STB

// ── GPIO mapping (rta::hal::InputSignal / OutputSignal -> pins) ─────
// Inputs (rider/body) — active-low pull-up, from architecture.md §9.2.
inline constexpr IfxPort_Pin kEstopPort   = IfxPort_Index_00; inline constexpr std::uint8_t kEstopPin   = 0;  // P00.0 X2-3
inline constexpr IfxPort_Pin kBrakeLeverPort = IfxPort_Index_00; inline constexpr std::uint8_t kBrakeLeverPin = 1;  // P00.1 X2-4
inline constexpr IfxPort_Pin kStartPort   = IfxPort_Index_00; inline constexpr std::uint8_t kStartPin   = 2;  // P00.2 X2-5
inline constexpr IfxPort_Pin kModePort    = IfxPort_Index_00; inline constexpr std::uint8_t kModePin    = 3;  // P00.3 X2-6
inline constexpr IfxPort_Pin kSwLeftPort  = IfxPort_Index_00; inline constexpr std::uint8_t kSwLeftPin  = 8;  // P00.8 X2-9
inline constexpr IfxPort_Pin kSwRightPort = IfxPort_Index_00; inline constexpr std::uint8_t kSwRightPin = 10; // P00.10 X2-11
inline constexpr IfxPort_Pin kSwHeadPort  = IfxPort_Index_00; inline constexpr std::uint8_t kSwHeadPin  = 11; // P00.11 X2-14

// Outputs (body) -> relays, from architecture.md §9.2.
inline constexpr IfxPort_Pin kLightLeftPort  = IfxPort_Index_33; inline constexpr std::uint8_t kLightLeftPin  = 10; // P33.10 X2-38
inline constexpr IfxPort_Pin kLightRightPort = IfxPort_Index_33; inline constexpr std::uint8_t kLightRightPin = 11; // P33.11 X1-3
inline constexpr IfxPort_Pin kBrakeLightPort = IfxPort_Index_33; inline constexpr std::uint8_t kBrakeLightPin = 12; // P33.12 X1-4
inline constexpr IfxPort_Pin kHeadlightPort  = IfxPort_Index_33; inline constexpr std::uint8_t kHeadlightPin  = 13; // P33.13 X1-5
inline constexpr IfxPort_Pin kBulbAutoPort   = IfxPort_Index_21; inline constexpr std::uint8_t kBulbAutoPin   = 4;  // P21.4 X1-19
inline constexpr IfxPort_Pin kBulbManualPort = IfxPort_Index_21; inline constexpr std::uint8_t kBulbManualPin = 5;  // P21.5 X1-22
inline constexpr IfxPort_Pin kRelay12vPort   = IfxPort_Index_21; inline constexpr std::uint8_t kRelay12vPin   = 0;  // P21.0 X1-15

// ── Watchdog WDI (TPS3850-Q1) ───────────────────────────────────────
// DESIGN-SELECTED: P33.1 / X2-29. The target Watchdog::service()
// performs the actual WDI pulse per the TPS3850-Q1 circuit.
inline constexpr IfxPort_Pin kWdtWdiPort = IfxPort_Index_33;
inline constexpr std::uint8_t kWdtWdiPin = 1;  // P33.1

}  // namespace rta::board
