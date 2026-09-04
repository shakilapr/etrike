#pragma once
// MTR STM32G431 — FDCAN1 Driver with FIFO0 RX ringbuffer and non-blocking TX
// Configures classic CAN 500 kbit/s with exact-match filters.

#include <cstdint>
#include <cstring>
#include "stm32g4xx_hal.h"
#include "config.h"
#include "protocol/compat/can.hpp"

namespace mtr {

class CanDriver {
public:
    CanDriver() = default;

    bool init() {
        // Configure FDCAN kernel clock to PCLK1 (16 MHz)
        RCC_PeriphCLKInitTypeDef periph_clk{};
        periph_clk.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
        periph_clk.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
        if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
            return false;
        }

        __HAL_RCC_FDCAN_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        // 1. Configure FDCAN GPIO pins PA11 (RX) and PA12 (TX)
        GPIO_InitTypeDef gpio{};
        gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF9_FDCAN1;
        HAL_GPIO_Init(GPIOA, &gpio);

        // 2. Configure FDCAN peripheral
        hfdcan_.Instance = FDCAN1;
        hfdcan_.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
        hfdcan_.Init.Mode = FDCAN_MODE_NORMAL;
        hfdcan_.Init.AutoRetransmission = ENABLE;
        hfdcan_.Init.TransmitPause = DISABLE;
        hfdcan_.Init.ProtocolException = DISABLE;

        // Bit timing for 500 kbps @ 16 MHz kernel:
        // Prescaler 2 -> Tq = 125 ns. (1 + 13 + 2) = 16 Tq = 2 us -> 500 kbps (87.5% sample point)
        hfdcan_.Init.NominalPrescaler = 2;
        hfdcan_.Init.NominalSyncJumpWidth = 2;
        hfdcan_.Init.NominalTimeSeg1 = 13;
        hfdcan_.Init.NominalTimeSeg2 = 2;

        hfdcan_.Init.DataPrescaler = 1;
        hfdcan_.Init.DataSyncJumpWidth = 1;
        hfdcan_.Init.DataTimeSeg1 = 1;
        hfdcan_.Init.DataTimeSeg2 = 1;

        hfdcan_.Init.StdFiltersNbr = 6; // 0x001, 0x110, 0x204, 0x0BB, 0x0AA, 0x112
        hfdcan_.Init.ExtFiltersNbr = 0;
        hfdcan_.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

        if (HAL_FDCAN_Init(&hfdcan_) != HAL_OK) {
            return false;
        }

        // 3. Configure Hardware Standard Filters (Routing to FIFO 0)
        configure_filter_(0, 0x001); // SAFETY_ESTOP
        configure_filter_(1, 0x110); // SYS_MODE_CMD
        configure_filter_(2, 0x204); // RT_DRIVE_CMD
        configure_filter_(3, 0x0BB); // Legacy relay state
        configure_filter_(4, 0x0AA); // Legacy throttle
        configure_filter_(5, 0x112); // HMI_PWR_REQ (Ignition)

        // 4. Configure Global Filter: reject non-matching standard frames
        HAL_FDCAN_ConfigGlobalFilter(&hfdcan_, FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);

        // 5. NVIC Interrupt Configuration
        HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);

        // 6. Start FDCAN and enable RX FIFO 0 New Message interrupt
        if (HAL_FDCAN_Start(&hfdcan_) != HAL_OK) {
            return false;
        }

        if (HAL_FDCAN_ActivateNotification(&hfdcan_, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
            return false;
        }

        initialized_ = true;
        return true;
    }

    // Transmit a canonical Frame
    bool send(const can::Frame& frame) {
        if (!initialized_) return false;

        FDCAN_TxHeaderTypeDef header{};
        header.Identifier = frame.id;
        header.IdType = frame.extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
        header.TxFrameType = FDCAN_DATA_FRAME;
        header.DataLength = dlc_to_fdcan_(frame.dlc);
        header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        header.BitRateSwitch = FDCAN_BRS_OFF;
        header.FDFormat = FDCAN_CLASSIC_CAN;
        header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
        header.MessageMarker = 0;

        uint8_t data[8]{};
        if (frame.dlc > 0 && frame.dlc <= 8) {
            std::memcpy(data, frame.data.data(), frame.dlc);
        }

        if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan_, &header, data) != HAL_OK) {
            tx_dropped_++;
            return false;
        }

        tx_count_++;
        return true;
    }

    // Called from ISR or Polling loop to extract next received frame
    bool poll_rx(can::Frame& out) {
        if (rx_head_ == rx_tail_) return false;

        out = rx_ring_[rx_tail_];
        rx_tail_ = (rx_tail_ + 1) % kRxRingSize;
        return true;
    }

    // ISR Callback hook
    void handle_rx_fifo0_isr() {
        FDCAN_RxHeaderTypeDef rx_hdr{};
        uint8_t rx_data[8]{};

        while (HAL_FDCAN_GetRxMessage(&hfdcan_, FDCAN_RX_FIFO0, &rx_hdr, rx_data) == HAL_OK) {
            uint8_t dlc = fdcan_to_dlc_(rx_hdr.DataLength);
            can::Frame frame(rx_hdr.Identifier, rx_hdr.IdType == FDCAN_EXTENDED_ID, dlc);
            if (dlc > 0 && dlc <= 8) {
                std::memcpy(frame.data.data(), rx_data, dlc);
            }

            uint16_t next_head = (rx_head_ + 1) % kRxRingSize;
            if (next_head != rx_tail_) {
                rx_ring_[rx_head_] = frame;
                rx_head_ = next_head;
            } else {
                rx_overflow_++;
            }
        }
    }

    FDCAN_HandleTypeDef* handle() { return &hfdcan_; }

    uint32_t tx_count() const { return tx_count_; }
    uint32_t tx_dropped() const { return tx_dropped_; }
    uint32_t rx_overflow() const { return rx_overflow_; }

private:
    void configure_filter_(uint32_t index, uint32_t id) {
        FDCAN_FilterTypeDef filter{};
        filter.IdType = FDCAN_STANDARD_ID;
        filter.FilterIndex = index;
        filter.FilterType = FDCAN_FILTER_MASK;
        filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
        filter.FilterID1 = id;
        filter.FilterID2 = 0x7FF; // Exact match mask
        HAL_FDCAN_ConfigFilter(&hfdcan_, &filter);
    }

    static uint32_t dlc_to_fdcan_(uint8_t dlc) {
        switch (dlc) {
        case 0: return FDCAN_DLC_BYTES_0;
        case 1: return FDCAN_DLC_BYTES_1;
        case 2: return FDCAN_DLC_BYTES_2;
        case 3: return FDCAN_DLC_BYTES_3;
        case 4: return FDCAN_DLC_BYTES_4;
        case 5: return FDCAN_DLC_BYTES_5;
        case 6: return FDCAN_DLC_BYTES_6;
        case 7: return FDCAN_DLC_BYTES_7;
        case 8:
        default: return FDCAN_DLC_BYTES_8;
        }
    }

    static uint8_t fdcan_to_dlc_(uint32_t fdcan_dlc) {
        switch (fdcan_dlc) {
        case FDCAN_DLC_BYTES_0: return 0;
        case FDCAN_DLC_BYTES_1: return 1;
        case FDCAN_DLC_BYTES_2: return 2;
        case FDCAN_DLC_BYTES_3: return 3;
        case FDCAN_DLC_BYTES_4: return 4;
        case FDCAN_DLC_BYTES_5: return 5;
        case FDCAN_DLC_BYTES_6: return 6;
        case FDCAN_DLC_BYTES_7: return 7;
        default: return 8;
        }
    }

    static constexpr uint16_t kRxRingSize = 32;
    can::Frame rx_ring_[kRxRingSize]{};
    volatile uint16_t rx_head_{0};
    volatile uint16_t rx_tail_{0};

    FDCAN_HandleTypeDef hfdcan_{};
    bool initialized_{false};
    uint32_t tx_count_{0};
    uint32_t tx_dropped_{0};
    uint32_t rx_overflow_{0};
};

}  // namespace mtr
