# Architecture Change Specification: STM32CubeIDE Native Toolchain & Hardware Multiplexing

**Document Version:** 2.0.0  
**Primary Software Suite:** **STM32CubeIDE / STM32CubeMX (STMicroelectronics Toolchain)**  
**Target Hardware:** WeAct Studio STM32G431CBU6 Core Board  
**Target Architecture:** Analog & Digital Multiplexing (Input Muxing + Output Decoder/Demuxing)  
**Location:** `control-toolkit/docs/architecture_change.md`

---

## 1. Toolchain Transition: PlatformIO to ST Software Suite

To align with STMicroelectronics ecosystem standards and vendor toolchains, the motor controller firmware is fully standardized on **STM32CubeIDE** and **STM32CubeMX** (`.ioc` project file), discarding PlatformIO dependency.

### 1.1 Project Structure (STM32Cube Native Layout)

```
mtr-stm32-v2/
├── VCU.ioc                     # STM32CubeMX Graphical Peripheral Configuration
├── .mxproject                  # CubeIDE Project Workspace Configuration
├── STM32G431CBUX_FLASH.ld      # ST Linker Script (Flash 128KB, RAM 32KB)
└── Core/
    ├── Inc/
    │   ├── main.h              # Pin Macro Defines & Peripheral Handles
    │   ├── stm32g4xx_hal_conf.h# STM32 HAL Module Configuration
    │   └── stm32g4xx_it.h      # Interrupt Service Routine Prototypes
    └── Src/
        ├── main.c              # Core Bare-Metal Control Loop & Muxing State Machine
        ├── stm32g4xx_hal_msp.c # MSP Initialization (FDCAN1, Clock & GPIO Clocks)
        ├── stm32g4xx_it.c      # FDCAN1 & SysTick Hardware Interrupt Handlers
        └── system_stm32g4xx.c  # System Clock Setup (HSI 16MHz / PLL 170MHz)
```

---

## 2. Hardware Multiplexing Architecture

To expand physical pin capacity while maintaining the compact 48-pin STM32G431CBU6 package, the hardware architecture incorporates **Analog Input Multiplexing** and **Output Demultiplexing**.

```
                           +-------------------------------------+
                           |    STM32G431CBU6 (WeAct Core)       |
                           +------------------+------------------+
                                              |
            +---------------------------------+---------------------------------+
            |                                                                   |
            v (3-Bit Select Lines: PB10, PB11, PB12)                            v (Select: PA0, PA1, Enable: PB0)
+-----------------------+                                           +-----------------------+
|  74HC4051 Analog MUX  |                                           | 74HC139 Output DEMUX  |
| 8 Channel Input Switch|                                           | 2-to-4 Relay Decoder  |
+-----------+-----------+                                           +-----------+-----------+
            | (PA0 ADC1_IN1)                                                    |
            v                                                                   v
 Throttle Grip, Brake Pressure,                                      72V Contactor Relays
 Temp Sensors, Gear Switch Array                                     (Neutral, Drive, Sport, Rev)
```

### 2.1 Analog & Digital Input Multiplexing (74HC4051 8-Channel MUX)

An 8-channel analog multiplexer (74HC4051) routes multiple analog and digital signals into a single ADC input (`PA0` / `ADC1_IN1`).

#### MUX Channel Allocation & Select Pin Mapping

* **Select Line S0**: `PB10` (`MUX_S0_PIN`)
* **Select Line S1**: `PB11` (`MUX_S1_PIN`)
* **Select Line S2**: `PB12` (`MUX_S2_PIN`)
* **Common Signal Pin (Z)**: `PA0` (STM32 ADC1 Channel 1)

| MUX Channel | `S2` (`PB12`) | `S1` (`PB11`) | `S0` (`PB10`) | Signal Connected | Processing Type |
| :---: | :---: | :---: | :---: | :--- | :--- |
| **Y0** | `0` | `0` | `0` | **Throttle Grip Sensor** | Analog ADC (0.8V–4.8V range) |
| **Y1** | `0` | `0` | `1` | **Brake Pressure Sensor** | Analog ADC (0.5V–4.5V range) |
| **Y2** | `0` | `1` | `0` | **Motor Temp Thermistor** | NTC Resistor Divider ADC |
| **Y3** | `0` | `1` | `1` | **Controller Temp Sensor** | Analog Temp Sensor ADC |
| **Y4** | `1` | `0` | `0` | **Gear Switch: Neutral** | Digital Voltage Level Check |
| **Y5** | `1` | `0` | `1` | **Gear Switch: Drive** | Digital Voltage Level Check |
| **Y6** | `1` | `1` | `0` | **Gear Switch: Sport** | Digital Voltage Level Check |
| **Y7** | `1` | `1` | `1` | **Gear Switch: Reverse** | Digital Voltage Level Check |

#### Software MUX Channel Selection & Sampling Sequence

```c
/* Channel Selection Helper in main.c */
static void selectMuxChannel(uint8_t channel)
{
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, (channel & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET); /* S0 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, (channel & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET); /* S1 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, (channel & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET); /* S2 */
  
  /* Settling time for analog MUX switch (~5 microseconds) */
  for (volatile int i = 0; i < 80; i++);
}

/* Cyclic Input Multiplexer Poll (Executed every 5ms) */
static void readMultiplexedInputs(void)
{
  /* 1. Sample Throttle Grip (Channel 0) */
  selectMuxChannel(0);
  rawThrottleAdc = readAdcDirect();

  /* 2. Sample Brake Pressure (Channel 1) */
  selectMuxChannel(1);
  rawBrakeAdc = readAdcDirect();

  /* 3. Sample Gear Switches (Channels 4 to 7) */
  selectMuxChannel(4); uint8_t n_val = (readAdcDirect() < 1000);
  selectMuxChannel(5); uint8_t d_val = (readAdcDirect() < 1000);
  selectMuxChannel(6); uint8_t s_val = (readAdcDirect() < 1000);
  selectMuxChannel(7); uint8_t r_val = (readAdcDirect() < 1000);

  /* Validate single gear selection (conflict check) */
  if ((n_val + d_val + s_val + r_val) > 1) {
    faultFlags |= (1 << 3); /* Gear Conflict */
  }
}
```

---

### 2.2 Output Demultiplexing & Decoder Control (74HC139 DEMUX)

To drive 72V gear contactors without risk of cross-conduction, a **74HC139 2-to-4 decoder/demultiplexer** converts 2 binary control lines into 4 mutually exclusive relay drive outputs.

#### DEMUX Output Selection Pin Map

* **Address Bit A (`DEC_A`)**: `PA0`
* **Address Bit B (`DEC_B`)**: `PA1`
* **Active-Low Enable (`DEC_EN`)**: `PB0`

| Target Gear State | `DEC_B` (`PA1`) | `DEC_A` (`PA0`) | `DEC_EN` (`PB0`) | Active Output Channel | Physical Contactor Energized |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **Neutral (`0`)** | `0` | `0` | `0` | **Y0** | Neutral Relay Active / Contactor Open |
| **Drive (`1`)** | `0` | `1` | `0` | **Y1** | **72V Drive Relay Active** |
| **Sport (`2`)** | `1` | `0` | `0` | **Y2** | **72V Sport Relay Active** |
| **Reverse (`3`)** | `1` | `1` | `0` | **Y3** | **72V Reverse Relay Active** |
| **`MODE_ESTOP`** | X | X | **`1` (HIGH)** | **None (Disabled)** | **All Contactors Instantly Opened** |

---

## 3. Peripheral Configuration in ST Software Suite

### 3.1 FDCAN1 Configuration (via `VCU.ioc`)

* **Bit Rate**: 500 kbps (Nominal Prescaler = 2, TimeSeg1 = 13, TimeSeg2 = 2 for 16MHz clock).
* **Frame Format**: Classic CAN (`FDCAN_FRAME_CLASSIC`).
* **Interrupts**: `FDCAN_IT_RX_FIFO0_NEW_MESSAGE` enabled on `FDCAN1_IT0_IRQn`.
* **Hardware Acceptance Filters**:
  * Filter 0: `0x204` (VCU Drive Command)
  * Filter 1: `0x001` (Safety ESTOP)
  * Filter 2: `0x205` (Brake Command)

### 3.2 Software Bit-Banged I2C (MCP4725 Throttle DAC)

To prevent HAL hardware I2C deadlocks, bit-banged I2C is implemented on open-drain GPIO pins:
* `PA5`: `I2C_SCL` (Output Open-Drain with internal/external pull-up)
* `PA7`: `I2C_SDA` (Output Open-Drain with internal/external pull-up)
* Output Pedestal: `0.8V` (`655`) to `4.8V` (`3931`). Forced to `0.0V` (`0`) on ESTOP.

---

## 4. Verification Plan for ST Toolchain Build

1. **ST STM32CubeIDE Build Validation**:
   * Compile project natively in STM32CubeIDE with zero warnings using GCC ARM toolchain (`arm-none-eabi-gcc`).
2. **Flash & Debug (ST-LINK V2 / V3)**:
   * Program target WeAct board using ST-LINK Utility / CubeIDE Debugger.
3. **Logic Analyzer Verification**:
   * Verify MUX select line timing (`PB10`, `PB11`, `PB12`) and DEMUX output switching (`PA0`, `PA1`, `PB0`).
