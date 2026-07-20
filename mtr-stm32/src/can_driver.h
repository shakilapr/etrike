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
//
// STM32 HAL integration:
//   - FDCAN handle "hfdcan1" defined externally (CubeMX generates it).
//   - RX FIFO0 interrupt gives semaphore / notifies task_can_rx.
//   - TX uses FIFO queue.

#include "protocol/core/frame.hpp"

namespace mtr {

class CanDriver {
public:
    /// Initialise the FDCAN peripheral.
    /// Requires CubeMX-generated CAN handle declared as: extern FDCAN_HandleTypeDef hfdcan1;
    bool init() {
        extern FDCAN_HandleTypeDef hfdcan1;
        if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) return false;
        if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) return false;
        m_initialized = true;
        return true;
    }

    /// Send a CAN frame (non-blocking, enters TX FIFO).
    bool send(const etrike::protocol::Frame& frame) {
        if (!m_initialized || !etrike::protocol::is_valid_frame(frame.view())) return false;
        extern FDCAN_HandleTypeDef hfdcan1;
        FDCAN_TxHeaderTypeDef tx = {};
        tx.Identifier = frame.id;
        tx.IdType = frame.extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
        tx.TxFrameType = FDCAN_DATA_FRAME;
        
        switch(frame.dlc) {
            case 0: tx.DataLength = FDCAN_DLC_BYTES_0; break;
            case 1: tx.DataLength = FDCAN_DLC_BYTES_1; break;
            case 2: tx.DataLength = FDCAN_DLC_BYTES_2; break;
            case 3: tx.DataLength = FDCAN_DLC_BYTES_3; break;
            case 4: tx.DataLength = FDCAN_DLC_BYTES_4; break;
            case 5: tx.DataLength = FDCAN_DLC_BYTES_5; break;
            case 6: tx.DataLength = FDCAN_DLC_BYTES_6; break;
            case 7: tx.DataLength = FDCAN_DLC_BYTES_7; break;
            case 8: default: tx.DataLength = FDCAN_DLC_BYTES_8; break;
        }
        
        tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        tx.BitRateSwitch = FDCAN_BRS_OFF;
        tx.FDFormat = FDCAN_CLASSIC_CAN;
        tx.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
        tx.MessageMarker = 0;
        
        uint8_t data[8];
        for (int i = 0; i < frame.dlc && i < 8; ++i) data[i] = frame.data[i];
        return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx, data) == HAL_OK;
    }

    /// Receive a CAN frame (polling).
    /// Returns true if a frame was available.
    bool receive(etrike::protocol::Frame& frame, uint32_t timeout_ticks = 10) {
        if (!m_initialized) return false;
        extern FDCAN_HandleTypeDef hfdcan1;
        if (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) == 0) return false;
        FDCAN_RxHeaderTypeDef rx = {};
        uint8_t data[8];
        if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rx, data) != HAL_OK) return false;
        
        frame.id       = rx.Identifier;
        frame.extended = (rx.IdType == FDCAN_EXTENDED_ID);
        
        switch(rx.DataLength) {
            case FDCAN_DLC_BYTES_0: frame.dlc = 0; break;
            case FDCAN_DLC_BYTES_1: frame.dlc = 1; break;
            case FDCAN_DLC_BYTES_2: frame.dlc = 2; break;
            case FDCAN_DLC_BYTES_3: frame.dlc = 3; break;
            case FDCAN_DLC_BYTES_4: frame.dlc = 4; break;
            case FDCAN_DLC_BYTES_5: frame.dlc = 5; break;
            case FDCAN_DLC_BYTES_6: frame.dlc = 6; break;
            case FDCAN_DLC_BYTES_7: frame.dlc = 7; break;
            case FDCAN_DLC_BYTES_8: frame.dlc = 8; break;
            default: frame.dlc = 8; break;
        }
        
        for (std::size_t i = 0; i < frame.dlc && i < frame.data.size(); ++i) frame.data[i] = data[i];
        return true;
    }


private:
    bool m_initialized = false;
};

/// Global CAN driver instance.
extern CanDriver g_can;

}  // namespace mtr
