#pragma once
// CAN driver — thin wrapper around STM32 bxCAN peripheral.
// Provides send/receive using the canonical protocol Frame type.
//
// STM32 HAL integration:
//   - CAN handle "hcan" defined externally (CubeMX generates it).
//   - RX FIFO0 interrupt gives semaphore / notifies task_can_rx.
//   - TX uses mailbox 0 with single-shot mode.
//
// Bit timing (APB1=36MHz → 500 kbit/s):
//   Prescaler=4, BS1=12, BS2=5, SJW=1 → 36MHz / 4 / (1+12+5) = 500 kHz

#include "protocol/core/frame.hpp"

namespace mtr {

class CanDriver {
public:
    /// Initialise the bxCAN peripheral.
    /// Requires CubeMX-generated CAN handle declared as: extern CAN_HandleTypeDef hcan;
    bool init() {
        extern CAN_HandleTypeDef hcan;
        if (HAL_CAN_Start(&hcan) != HAL_OK) return false;
        if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) return false;
        m_initialized = true;
        return true;
    }

    /// Send a CAN frame (non-blocking, enters TX mailbox).
    bool send(const etrike::protocol::Frame& frame) {
        if (!m_initialized || !etrike::protocol::is_valid_frame(frame.view())) return false;
        extern CAN_HandleTypeDef hcan;
        CAN_TxHeaderTypeDef tx = {};
        if (frame.extended) tx.ExtId = frame.id;
        else tx.StdId = frame.id;
        tx.IDE   = frame.extended ? CAN_ID_EXT : CAN_ID_STD;
        tx.DLC   = frame.dlc;
        tx.TransmitGlobalTime = DISABLE;
        uint32_t mailbox;
        uint8_t data[8];
        for (int i = 0; i < frame.dlc && i < 8; ++i) data[i] = frame.data[i];
        return HAL_CAN_AddTxMessage(&hcan, &tx, data, &mailbox) == HAL_OK;
    }

    /// Receive a CAN frame (polling).
    /// Returns true if a frame was available.
    bool receive(etrike::protocol::Frame& frame, uint32_t timeout_ticks = 10) {
        if (!m_initialized) return false;
        extern CAN_HandleTypeDef hcan;
        if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) == 0) return false;
        CAN_RxHeaderTypeDef rx = {};
        uint8_t data[8];
        if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rx, data) != HAL_OK) return false;
        frame.id       = rx.IDE == CAN_ID_EXT ? rx.ExtId : rx.StdId;
        frame.extended = (rx.IDE == CAN_ID_EXT);
        frame.dlc      = rx.DLC;
        for (int i = 0; i < rx.DLC && i < 8; ++i) frame.data[i] = data[i];
        return true;
    }


private:
    bool m_initialized = false;
};

/// Global CAN driver instance.
extern CanDriver g_can;

}  // namespace mtr
