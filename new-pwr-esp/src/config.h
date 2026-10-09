#pragma once
// new-pwr-esp — Dedicated DCDC Power Supply Keepalive Controller
// Target: ESP32-WROOM-32UE
// Transceiver: WCMCU-230 (SN65HVD230)

#include <cstdint>
#include <array>
#include "protocol/core/frame.hpp"

namespace pwr {

// ── Default Pin Configuration ───────────────────────────────────────
// Printed TX (GPIO 1) and RX (GPIO 3) header pins on ESP32 board
#ifndef CONFIG_CAN_TX_PIN
#define CONFIG_CAN_TX_PIN 1
#endif

#ifndef CONFIG_CAN_RX_PIN
#define CONFIG_CAN_RX_PIN 3
#endif

#ifndef CONFIG_CAN_BITRATE_HZ
#define CONFIG_CAN_BITRATE_HZ 500000 // 500 kbps bus rate
#endif

constexpr int kCanTxGpio    = CONFIG_CAN_TX_PIN;
constexpr int kCanRxGpio    = CONFIG_CAN_RX_PIN;
constexpr int kCanBitrateHz = CONFIG_CAN_BITRATE_HZ;

// Visual pulse indicator (onboard LED, GPIO 2 on standard ESP32)
constexpr int kStatusLedGpio = 2;

// ── DCDC Keepalive Frame Definition ─────────────────────────────────
// ID: 0x10262B27 (29-bit extended frame)
// Rate: 10 Hz (every 100 ms)
// Payload: {0xFD, 0x78, 0x00, 0xC8, 0x00, 0xFF, 0xFF, 0xFC}
constexpr uint32_t kDcdcCanId = 0x10262B27u;
constexpr uint8_t  kDcdcDlc   = 8;
constexpr std::array<uint8_t, 8> kDcdcData = {
    0xFD, 0x78, 0x00, 0xC8, 0x00, 0xFF, 0xFF, 0xFC
};

} // namespace pwr

namespace can {
using Frame = etrike::protocol::Frame;
}
