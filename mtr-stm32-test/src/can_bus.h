#pragma once
// MTR STM432 test firmware — Minimal FDCAN1 Classic CAN driver for bench testing.
// Mirrors the production mtr-stm32 CAN configuration (same pins, same bit
// timing, same fail-fast transmit policy) so test results transfer 1:1 to the
// vehicle firmware. No protocol dependencies; raw classic frames only.

#include <cstdint>
#include "stm32g4xx_hal.h"
#include "config.h"

extern "C" {
    extern FDCAN_HandleTypeDef hfdcan1;
}

namespace mtr_test {

class CanBus {
public:
    // Last TX/protocol outcome, refreshed by service() — the single PSR/ECR
    // reader (PSR.LEC is read-destructive; no other code may touch PSR).
    struct Health {
        uint8_t tec{0};         // Transmit Error Counter (ECR[7:0])
        uint8_t lec{7};         // Last Error Code (PSR[2:0]); 0 or 7 = clean
        bool    bus_off{false}; // PSR.BO
    };

    bool init(bool enable_rx) {
        // FDCAN kernel clock = PCLK1 (16 MHz), identical to production
        RCC_PeriphCLKInitTypeDef periph_clk{};
        periph_clk.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
        periph_clk.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
        if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
            return false;
        }

        __HAL_RCC_FDCAN_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        // PA11 = FDCAN1_RX, PA12 = FDCAN1_TX (AF9), identical to production
        GPIO_InitTypeDef gpio{};
        gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF9_FDCAN1;
        HAL_GPIO_Init(GPIOA, &gpio);

        hfdcan1.Instance = FDCAN1;
        hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;  // Classic 2.0A on the wire
        hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
        hfdcan1.Init.AutoRetransmission = DISABLE;       // fail fast, like production
        hfdcan1.Init.TransmitPause = DISABLE;
        hfdcan1.Init.ProtocolException = DISABLE;

        // Production-identical nominal timing
        hfdcan1.Init.NominalPrescaler = kCanPrescaler;
        hfdcan1.Init.NominalSyncJumpWidth = kCanSJW;
        hfdcan1.Init.NominalTimeSeg1 = kCanSeg1;
        hfdcan1.Init.NominalTimeSeg2 = kCanSeg2;

        // Data-phase registers unused in classic mode
        hfdcan1.Init.DataPrescaler = 1;
        hfdcan1.Init.DataSyncJumpWidth = 1;
        hfdcan1.Init.DataTimeSeg1 = 1;
        hfdcan1.Init.DataTimeSeg2 = 1;

        hfdcan1.Init.StdFiltersNbr = enable_rx ? 1u : 0u;
        hfdcan1.Init.ExtFiltersNbr = 0;
        hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

        if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK) {
            return false;
        }

        if (enable_rx) {
            // Accept all standard frames into RX FIFO 0 (passive listener)
            FDCAN_FilterTypeDef filter{};
            filter.IdType = FDCAN_STANDARD_ID;
            filter.FilterIndex = 0;
            filter.FilterType = FDCAN_FILTER_MASK;
            filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
            filter.FilterID1 = 0x000;
            filter.FilterID2 = 0x000;  // mask 0 = accept all
            if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) != HAL_OK) {
                return false;
            }
            HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                         FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                                         FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0);
            // RX is polled every loop pass (1 ms) — no interrupt needed at
            // bench traffic rates; the 64-deep hardware FIFO cannot overflow.
        } else {
            HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT,
                                         FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);
        }

        rx_enabled_ = enable_rx;
        return HAL_FDCAN_Start(&hfdcan1) == HAL_OK;
    }

    // Transmit a classic standard data frame (zero payload).
    bool send(uint32_t id, uint8_t dlc) {
        FDCAN_TxHeaderTypeDef header{};
        header.Identifier = id;
        header.IdType = FDCAN_STANDARD_ID;
        header.TxFrameType = FDCAN_DATA_FRAME;
        header.DataLength = dlc_to_fd_(dlc);
        header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        header.BitRateSwitch = FDCAN_BRS_OFF;
        header.FDFormat = FDCAN_CLASSIC_CAN;
        header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
        header.MessageMarker = 0;

        uint8_t data[8] = {0};
        return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &header, data) == HAL_OK;
    }

    // Poll one received standard frame (RX mode only). Returns false if empty.
    bool poll_rx(uint32_t &id, uint8_t &dlc) {
        FDCAN_RxHeaderTypeDef rx{};
        uint8_t data[8] = {0};
        if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rx, data) != HAL_OK) {
            return false;
        }
        id = rx.Identifier;
        dlc = fd_to_dlc_(rx.DataLength);
        return true;
    }

    // Call every loop pass (~1 ms). Single PSR/ECR read point + bus-off
    // recovery (ISO 11898-1: clearing CCCR.INIT triggers 128x11 recessive bits),
    // debounced like production.
    void service() {
        const uint32_t ecr = hfdcan1.Instance->ECR;
        const uint32_t psr = hfdcan1.Instance->PSR;
        health_.tec = static_cast<uint8_t>(ecr & 0xFFu);
        health_.lec = static_cast<uint8_t>(psr & FDCAN_PSR_LEC_Msk);
        health_.bus_off = (psr & FDCAN_PSR_BO) != 0u;

        if (health_.bus_off && (HAL_GetTick() - last_recovery_ms_ >= kBusOffRecoveryDebounceMs)) {
            last_recovery_ms_ = HAL_GetTick();
            CLEAR_BIT(hfdcan1.Instance->CCCR, FDCAN_CCCR_INIT);
        }
    }

    Health health() const { return health_; }

    // True while a TX request is still pending (frame not yet handed to the bus).
    bool tx_pending() const {
        return (hfdcan1.Instance->TXBRP & 0x1u) != 0u;
    }

private:
    static uint32_t dlc_to_fd_(uint8_t dlc) {
        switch (dlc) {
        case 0: return FDCAN_DLC_BYTES_0;
        case 1: return FDCAN_DLC_BYTES_1;
        case 2: return FDCAN_DLC_BYTES_2;
        case 3: return FDCAN_DLC_BYTES_3;
        case 4: return FDCAN_DLC_BYTES_4;
        case 5: return FDCAN_DLC_BYTES_5;
        case 6: return FDCAN_DLC_BYTES_6;
        case 7: return FDCAN_DLC_BYTES_7;
        default: return FDCAN_DLC_BYTES_8;
        }
    }

    static uint8_t fd_to_dlc_(uint32_t fd) {
        switch (fd) {
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

    Health health_{};
    bool rx_enabled_{false};
    uint32_t last_recovery_ms_{0};
};

}  // namespace mtr_test
