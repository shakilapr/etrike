#pragma once
// Lightweight STM32G4xx HAL Stub for Host-Native Testing
#include <cstdint>
#include <cstring>
#include <vector>
#include <deque>

typedef int HAL_StatusTypeDef;
#define HAL_OK       0x00U
#define HAL_ERROR    0x01U
#define HAL_BUSY     0x02U
#define HAL_TIMEOUT  0x03U

#define ENABLE  1
#define DISABLE 0

#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET   1

#define GPIO_MODE_OUTPUT_PP 0
#define GPIO_MODE_OUTPUT_OD 1
#define GPIO_MODE_AF_PP     2
#define GPIO_NOPULL         0
#define GPIO_PULLUP         1
#define GPIO_SPEED_FREQ_LOW       0
#define GPIO_SPEED_FREQ_HIGH      1
#define GPIO_SPEED_FREQ_VERY_HIGH 2

#define GPIO_PIN_11 (1 << 11)
#define GPIO_PIN_12 (1 << 12)
#define GPIO_AF9_FDCAN1 9

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

typedef struct {
    uint32_t dummy;
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef*)1)
#define GPIOB ((GPIO_TypeDef*)2)
#define GPIOC ((GPIO_TypeDef*)3)

#define __HAL_RCC_GPIOA_CLK_ENABLE() do {} while(0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() do {} while(0)
#define __HAL_RCC_FDCAN_CLK_ENABLE() do {} while(0)
#define __NOP() do {} while(0)

// ── Global Test Mock State for GPIO & Software I2C ─────────────────
namespace hal_mock {

inline uint16_t g_gpio_a_pins = 0;
inline uint16_t g_gpio_c_pins = 0;
inline uint32_t g_led_toggle_count = 0;

// Software I2C state machine state
inline uint8_t  g_i2c_target_addr = 0xC0; // 0x60 << 1
inline uint8_t  g_i2c_current_byte = 0;
inline uint8_t  g_i2c_bit_count = 0;
inline uint8_t  g_i2c_bytes[4] = {0, 0, 0, 0};
inline uint8_t  g_i2c_byte_idx = 0;
inline uint16_t g_last_dac_written = 0;
inline bool     g_i2c_in_start = false;
inline bool     g_i2c_ack_state = false;
inline bool     g_i2c_nack_address = false;

inline void reset() {
    g_gpio_a_pins = 0;
    g_gpio_c_pins = 0;
    g_led_toggle_count = 0;
    g_i2c_target_addr = 0xC0;
    g_i2c_current_byte = 0;
    g_i2c_bit_count = 0;
    g_i2c_byte_idx = 0;
    g_last_dac_written = 0;
    g_i2c_in_start = false;
    g_i2c_ack_state = false;
    g_i2c_nack_address = false;
}

} // namespace hal_mock

inline void HAL_GPIO_Init(GPIO_TypeDef* /*port*/, GPIO_InitTypeDef* /*cfg*/) {}

inline void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin_mask, int state) {
    if (port == GPIOA) {
        bool prev_scl = (hal_mock::g_gpio_a_pins & 0x0020) != 0;
        bool prev_sda = (hal_mock::g_gpio_a_pins & 0x0080) != 0;

        if (state == GPIO_PIN_SET) {
            hal_mock::g_gpio_a_pins |= pin_mask;
        } else {
            hal_mock::g_gpio_a_pins &= ~pin_mask;
        }

        bool new_scl = (hal_mock::g_gpio_a_pins & 0x0020) != 0;
        bool new_sda = (hal_mock::g_gpio_a_pins & 0x0080) != 0;

        // I2C START condition: SDA goes HIGH -> LOW while SCL is HIGH
        if (prev_sda && !new_sda && new_scl) {
            hal_mock::g_i2c_in_start = true;
            hal_mock::g_i2c_bit_count = 0;
            hal_mock::g_i2c_current_byte = 0;
            hal_mock::g_i2c_byte_idx = 0;
            hal_mock::g_i2c_ack_state = false;
        }

        // I2C STOP condition: SDA goes LOW -> HIGH while SCL is HIGH
        if (!prev_sda && new_sda && new_scl) {
            hal_mock::g_i2c_in_start = false;
            // Decode MCP4725 fast-write command if 4 bytes were received
            // Format: Byte 0 = Address, Byte 1 = Command (0x40), Byte 2 = Data[11:4], Byte 3 = Data[3:0]<<4
            if (hal_mock::g_i2c_byte_idx >= 4 && hal_mock::g_i2c_bytes[1] == 0x40) {
                uint16_t high = hal_mock::g_i2c_bytes[2];
                uint16_t low = hal_mock::g_i2c_bytes[3];
                hal_mock::g_last_dac_written = (high << 4) | (low >> 4);
            }
        }

        // Clock edge: SCL goes LOW -> HIGH (Sample bit)
        if (!prev_scl && new_scl && hal_mock::g_i2c_in_start && (pin_mask & 0x0020)) {
            if (hal_mock::g_i2c_byte_idx < 4) {
                if (hal_mock::g_i2c_bit_count < 8) {
                    hal_mock::g_i2c_current_byte = (hal_mock::g_i2c_current_byte << 1) | (new_sda ? 1 : 0);
                    hal_mock::g_i2c_bit_count++;
                    if (hal_mock::g_i2c_bit_count == 8) {
                        hal_mock::g_i2c_bytes[hal_mock::g_i2c_byte_idx++] = hal_mock::g_i2c_current_byte;
                    }
                } else {
                    // 9th clock: ACK bit cycle
                    hal_mock::g_i2c_bit_count = 0;
                    hal_mock::g_i2c_current_byte = 0;
                }
            }
        }
    } else if (port == GPIOC) {
        if (state == GPIO_PIN_SET) {
            hal_mock::g_gpio_c_pins |= pin_mask;
        } else {
            hal_mock::g_gpio_c_pins &= ~pin_mask;
        }
    }
}

inline int HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin_mask) {
    if (port == GPIOA) {
        // When reading SDA on PA7 during ACK clock cycle:
        // If the address matches target address (and not forced NACK), pull SDA low (RESET) for ACK!
        if (pin_mask == 0x0080) { // PA7 = SDA
            if (!hal_mock::g_i2c_nack_address) {
                uint8_t addr_written = hal_mock::g_i2c_bytes[0];
                if (addr_written == hal_mock::g_i2c_target_addr || hal_mock::g_i2c_byte_idx > 1) {
                    return GPIO_PIN_RESET; // ACK (SDA Low)
                }
            }
            return GPIO_PIN_SET; // NACK (SDA High)
        }
        return (hal_mock::g_gpio_a_pins & pin_mask) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    if (port == GPIOC) {
        return (hal_mock::g_gpio_c_pins & pin_mask) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    return GPIO_PIN_RESET;
}

inline void HAL_GPIO_TogglePin(GPIO_TypeDef* port, uint16_t pin_mask) {
    if (port == GPIOC && (pin_mask & 0x0040)) {
        hal_mock::g_gpio_c_pins ^= pin_mask;
        hal_mock::g_led_toggle_count++;
    }
}

// ── FDCAN Definitions & Stubs ───────────────────────────────────────
#define FDCAN_FRAME_CLASSIC     0
#define FDCAN_MODE_NORMAL       0
#define FDCAN_TX_FIFO_OPERATION 0
#define FDCAN_STANDARD_ID       0
#define FDCAN_EXTENDED_ID       1
#define FDCAN_FILTER_MASK       0
#define FDCAN_FILTER_TO_RXFIFO0 0
#define FDCAN_REJECT            0
#define FDCAN_FILTER_REMOTE     0
#define FDCAN_REJECT_REMOTE     1
#define FDCAN1_IT0_IRQn         0
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE (1 << 0)
#define FDCAN_DATA_FRAME        0
#define FDCAN_ESI_ACTIVE        0
#define FDCAN_BRS_OFF           0
#define FDCAN_CLASSIC_CAN       0
#define FDCAN_NO_TX_EVENTS      0
#define FDCAN_RX_FIFO0          0

#define FDCAN_DLC_BYTES_0 0
#define FDCAN_DLC_BYTES_1 1
#define FDCAN_DLC_BYTES_2 2
#define FDCAN_DLC_BYTES_3 3
#define FDCAN_DLC_BYTES_4 4
#define FDCAN_DLC_BYTES_5 5
#define FDCAN_DLC_BYTES_6 6
#define FDCAN_DLC_BYTES_7 7
#define FDCAN_DLC_BYTES_8 8

typedef struct {
    volatile uint32_t CCCR;
    volatile uint32_t PSR;
} FDCAN_GlobalTypeDef;

#define FDCAN_PSR_BO    (1 << 7)
#define FDCAN_CCCR_INIT (1 << 0)
#define CLEAR_BIT(REG, BIT) ((REG) &= ~(BIT))

#define RCC_PERIPHCLK_FDCAN 1
#define RCC_FDCANCLKSOURCE_PCLK1 1

inline FDCAN_GlobalTypeDef g_fdcan1_regs{};
#define FDCAN1 (&g_fdcan1_regs)

typedef struct {
    uint32_t PeriphClockSelection;
    uint32_t FdcanClockSelection;
} RCC_PeriphCLKInitTypeDef;

typedef struct {
    uint32_t IdType;
    uint32_t FilterIndex;
    uint32_t FilterType;
    uint32_t FilterConfig;
    uint32_t FilterID1;
    uint32_t FilterID2;
} FDCAN_FilterTypeDef;

typedef struct {
    uint32_t Identifier;
    uint32_t IdType;
    uint32_t TxFrameType;
    uint32_t DataLength;
    uint32_t ErrorStateIndicator;
    uint32_t BitRateSwitch;
    uint32_t FDFormat;
    uint32_t TxEventFifoControl;
    uint32_t MessageMarker;
} FDCAN_TxHeaderTypeDef;

typedef struct {
    uint32_t Identifier;
    uint32_t IdType;
    uint32_t RxFrameType;
    uint32_t DataLength;
    uint32_t ErrorStateIndicator;
    uint32_t BitRateSwitch;
    uint32_t FDFormat;
    uint32_t IsFilterMatchingFrame;
    uint32_t FilterIndex;
} FDCAN_RxHeaderTypeDef;

typedef struct {
    uint32_t FrameFormat;
    uint32_t Mode;
    uint32_t AutoRetransmission;
    uint32_t TransmitPause;
    uint32_t ProtocolException;
    uint32_t NominalPrescaler;
    uint32_t NominalSyncJumpWidth;
    uint32_t NominalTimeSeg1;
    uint32_t NominalTimeSeg2;
    uint32_t DataPrescaler;
    uint32_t DataSyncJumpWidth;
    uint32_t DataTimeSeg1;
    uint32_t DataTimeSeg2;
    uint32_t StdFiltersNbr;
    uint32_t ExtFiltersNbr;
    uint32_t TxFifoQueueMode;
} FDCAN_InitTypeDef;

typedef enum {
    HAL_FDCAN_STATE_RESET = 0x00U,
    HAL_FDCAN_STATE_READY = 0x01U,
    HAL_FDCAN_STATE_BUSY  = 0x02U,
    HAL_FDCAN_STATE_ERROR = 0x03U
} HAL_FDCAN_StateTypeDef;

typedef struct {
    FDCAN_GlobalTypeDef* Instance;
    FDCAN_InitTypeDef Init;
    HAL_FDCAN_StateTypeDef State;
} FDCAN_HandleTypeDef;

// Global FDCAN Mock Buffers
namespace fdcan_mock {
struct MockMsg {
    uint32_t id;
    bool ext;
    uint8_t dlc;
    uint8_t data[8];
};

inline std::vector<MockMsg> g_tx_msgs;
inline std::deque<MockMsg>  g_rx_queue;

inline void reset() {
    g_tx_msgs.clear();
    g_rx_queue.clear();
    g_fdcan1_regs.CCCR = 0;
    g_fdcan1_regs.PSR = 0;
}
} // namespace fdcan_mock

inline HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef*) { return HAL_OK; }
inline HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef* hfdcan) {
    if (hfdcan) hfdcan->State = HAL_FDCAN_STATE_READY;
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef*, FDCAN_FilterTypeDef*) { return HAL_OK; }
inline HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef*, uint32_t, uint32_t, uint32_t, uint32_t) { return HAL_OK; }
inline void HAL_NVIC_SetPriority(uint32_t, uint32_t, uint32_t) {}
inline void HAL_NVIC_EnableIRQ(uint32_t) {}
inline void HAL_NVIC_DisableIRQ(uint32_t) {}
inline HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef* hfdcan) {
    if (hfdcan) hfdcan->State = HAL_FDCAN_STATE_BUSY;
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef* hfdcan) {
    if (hfdcan) hfdcan->State = HAL_FDCAN_STATE_READY;
    return HAL_OK;
}
inline HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef*, uint32_t, uint32_t) { return HAL_OK; }

inline HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef*, FDCAN_TxHeaderTypeDef* header, uint8_t* data) {
    fdcan_mock::MockMsg m{};
    m.id = header->Identifier;
    m.ext = (header->IdType == FDCAN_EXTENDED_ID);
    m.dlc = static_cast<uint8_t>(header->DataLength);
    if (m.dlc > 8) m.dlc = 8;
    if (data) std::memcpy(m.data, data, m.dlc);
    fdcan_mock::g_tx_msgs.push_back(m);
    return HAL_OK;
}

inline HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef*, uint32_t, FDCAN_RxHeaderTypeDef* header, uint8_t* data) {
    if (fdcan_mock::g_rx_queue.empty()) {
        return HAL_ERROR;
    }
    const auto& m = fdcan_mock::g_rx_queue.front();
    header->Identifier = m.id;
    header->IdType = m.ext ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    header->DataLength = m.dlc;
    if (data && m.dlc > 0) {
        std::memcpy(data, m.data, m.dlc);
    }
    fdcan_mock::g_rx_queue.pop_front();
    return HAL_OK;
}
