# VCU_2 — STM32G431 Vehicle Control Unit: Architecture

> Root of this document is the STM32 (vehicle-side) node of the e-trike project.
> The companion ESP32 host node lives outside this repo. This firmware is a **CAN slave / actuator node**:
> it receives commands over FDCAN and drives 3 active-low relays + up to 3 MCP4725 DACs (bit-banged I2C).

---

## 1. Project overview

| Field | Value |
|---|---|
| Project name | `VCU_2` |
| Silicon | STM32G431CBU6 (STM32G4 family), `STM32G431CBUx` Cube target |
| Package | UFQFPN48 (48 pins) |
| Toolchain | STM32CubeIDE, GCC `arm-none-eabi` 13.3.rel1 |
| HAL / FW pack | STM32Cube FW_G4 **V1.6.3**, CubeMX 6.15.0 |
| Linker script | `STM32G431CBUX_FLASH.ld` |
| Debug config | `VCU_2 Debug.launch`, artifact `VCU_2.elf` |
| System clock | 16 MHz, HSI direct (no PLL, no HSE) |
| Flash / RAM | 128 KB / 32 KB |
| Role on bus | Classic CAN @ 500 kbps, **receive-only in practice** (TX pin/fifo configured, no Tx code); 2 Rx filters (0x0BB, 0x0AA) |

The MCU converts two ESP32-originated CAN frames into physical actions:

- `0x0BB` — **Digital Vehicle State / Relays**: byte `[0]` encodes ignition + gear selection (with optional brake bit). Firmware asserts active-low relay driver pins on PA0/PA2/PA4 and toggles the PC6 status LED when the value changes.
- `0x0AA` — **Throttle / Drive-by-Wire**: bytes `[0:1]` are a big-endian 16-bit throttle. Firmware converts to a 12-bit DAC code, clamps it, and writes it to a MCP4725 via software-I2C on PA5(SCL)/PA7(SDA).

A **comms watchdog** (500 ms, armed only after the first message) de-energizes everything on CAN silence.

---

## 2. Source tree (what actually builds)

Everything under `Debug/objects.list` is compiled; the rest is ST-library glue.

```
mtr-stm/
├── VCU_2.ioc                    CubeMX project (NVIC + RCC only, pins are code-defined)
├── STM32G431CBUX_FLASH.ld       Linker script (RAM 32K @0x20000000, FLASH 128K @0x08000000)
├── VCU_2 Debug.launch           STM32CubeIDE debug launch
├── .cproject / .project         Eclipse managed build (Debug opt=-g3; Release opt=-Os)
├── Core/
│   ├── Inc/
│   │   ├── main.h               HAL include + Error_Handler() prototype
│   │   ├── stm32g4xx_it.h       IRQ prototypes
│   │   └── stm32g4xx_hal_conf.h HAL module enable/disable switches
│   ├── Startup/startup_stm32g431cbux.s   Reset_Handler, vector table
│   └── Src/
│       ├── main.c               ⭐ all application logic (406 lines)
│       ├── main.c.bak           previous behaviour (see §11 diff) — NOT built
│       ├── stm32g4xx_it.c       IRQ dispatchers (FDCAN1_IT0)
│       ├── stm32g4xx_hal_msp.c  HAL_MspInit (SYSCFG+PWR clocks)
│       ├── syscalls.c / sysmem.c  newlib stubs
│       ├── system_stm32g4xx.c   SystemInit/SystemCoreClock (HSI 16MHz)
│       └── *.code-workspace     VSCode multi-root leftovers — not built
├── Drivers/ (CMSIS + STM32G4xx_HAL_Driver)  vendor library
└── Debug/                       build output (elf/map/list) + generated makefiles
```

Sources in the link (from `Debug/objects.list`): `main.c`, `stm32g4xx_it.c`,
`stm32g4xx_hal_msp.c`, `syscalls.c`, `sysmem.c`, `system_stm32g4xx.c`,
`startup_stm32g431cbux.s`, plus the HAL modules `hal`, `hal_cortex`, `hal_dma`,
`hal_dma_ex`, `hal_exti`, `hal_fdcan`, `hal_flash`, `hal_flash_ex`,
`hal_flash_ramfunc`, `hal_gpio`, `hal_pwr`, `hal_pwr_ex`, `hal_rcc`, `hal_rcc_ex`.

---

## 3. Clock configuration

From `main.c:SystemClock_Config()` and `.ioc`:

- Core voltage range: `PWR_REGULATOR_VOLTAGE_SCALE1`.
- Oscillator: **HSI 16 MHz**, calibration default, **PLL disabled** (`RCC_PLL_NONE`).
- `SYSCLK = HSI = 16 MHz`; AHB div 1, APB1 div 1, APB2 div 1 → all buses at 16 MHz.
- FLASH latency 0 wait states (`FLASH_LATENCY_0`).

Key derived clock (feeds the whole design): **FDCAN kernel clock = PCLK1 = 16 MHz** (selected `RCC_FDCANCLKSOURCE_PCLK1` in `HAL_FDCAN_MspInit`).

> Note: CubeMX `.ioc` RCC values confirm HSI 16 MHz; the compiled system clock is 16 MHz, so SysTick
> ticks every 1 ms (`HAL_GetTick`). DWT is enabled in `main()` but the cycle counter is **never read**
> by any code (the bit-bang delays use NOP loops, not CYCCNT).

### Clock-tree table (per `.ioc` RCC values)

| Signal | Freq | Notes |
|---|---|---|
| HSI | 16 MHz | system clock source |
| SYSCLK/HCLK/FCLK | 16 MHz | AHB div 1 |
| PCLK1/APB1 | 16 MHz | APB1 div 1 |
| PCLK2/APB2 | 16 MHz | APB2 div 1 |
| APB1 timers / APB2 timers | 16 MHz | div 1 → no ×2 |
| FDCAN kernel | 16 MHz | = PCLK1 |
| HSI48 | 48 MHz | declared, but no USB/RNG peripheral enabled → unused |
| LSI | 32 kHz | declared in HAL conf, unused (no IWDG/RTC) |
| LSE | 32768 Hz | declared in HAL conf, unused |

---

## 4. Memory map (linker `STM32G431CBUX_FLASH.ld`)

| Region | Origin | Length |
|---|---|---|
| RAM (xrw) | `0x20000000` | 32 K |
| FLASH (rx) | `0x08000000` | 128 K |

- `_estack` = end of RAM (`0x20000000 + 0x8000` = `0x20008000`), initial SP.
- `_Min_Heap_Size = 0x200` (512 B), `_Min_Stack_Size = 0x400` (1 KB).
- Sections: `.isr_vector`, `.text`, `.rodata`, `.ARM.extab`, `.ARM`, init/fini arrays,
  `.data` (VMA RAM / LMA FLASH), `.bss`, then a combined `._user_heap_stack`.
- `.RAM` etc not used; no custom `RamFunc`.
- Reset_Handler (startup asm) copies `.data` from `_sidata`, zero-fills `.bss`,
  calls `SystemInit`, `__libc_init_array`, then `main`; traps in a loop if `main` returns.

---

## 5. Compile-time definitions & key constants

From `.cproject`: `DEBUG`, `USE_HAL_DRIVER`, `STM32G431xx`. Include paths: `Core/Inc`,
HAL `Inc`, HAL `Legacy`, CMSIS Device Include, CMSIS Include. FPU: hard-float
`fpv4-sp-d16` (though this app does no float math). GCC optimization: `-g3` (Debug).

### HAL config highlights (`stm32g4xx_hal_conf.h`)

- Modules enabled: `HAL_MODULE_ENABLED`, `HAL_FDCAN_MODULE_ENABLED`,
  `HAL_GPIO_MODULE_ENABLED`, `HAL_EXTI_MODULE_ENABLED`, `HAL_DMA_MODULE_ENABLED`,
  `HAL_RCC_MODULE_ENABLED`, `HAL_FLASH_MODULE_ENABLED`, `HAL_PWR_MODULE_ENABLED`,
  `HAL_CORTEX_MODULE_ENABLED`.
- **I2C HW module disabled** → the firmware's software-I2C is the only I2C.
- `HSE_VALUE 8 MHz`, `HSI_VALUE 16 MHz`, `HSI48_VALUE 48 MHz`, `LSI 32 kHz`, `LSE 32768`.
- `VDD_VALUE 3300 mV`, `TICK_INT_PRIORITY 15`, no RTOS, `PREFETCH 0`,
  I-cache ON, D-cache ON, `USE_FULL_ASSERT` off (`assert_param` is a no-op).
- All `USE_HAL_*_REGISTER_CALLBACKS` = 0 → the weak `__weak` callbacks (e.g.
  `HAL_FDCAN_RxFifo0Callback`) are invoked directly; no runtime callback registration.

### Application constants (`main.c` `#define`s)

| Macro | Value | Meaning |
|---|---|---|
| `MODE_DRIVE_PIN/PORT` | `GPIO_PIN_2` / `GPIOA` | Drive relay drive, active-low |
| `IGNITION_PIN/PORT` | `GPIO_PIN_4` / `GPIOA` | Ignition relay drive, active-low |
| `MODE_REVERSE_PIN/PORT` | `GPIO_PIN_0` / `GPIOA` | Reverse relay drive, active-low |
| `I2C_SCL_PIN/PORT` | `GPIO_PIN_5` / `GPIOA` | bit-bang I2C clock |
| `I2C_SDA_PIN/PORT` | `GPIO_PIN_7` / `GPIOA` | bit-bang I2C data |
| `DAC_MIN_VAL` | `655U` | lower clamp = 12-bit code ≈ 0.8 V @ 5.0 V reference |
| `DAC_MAX_VAL` | `1966U` | upper clamp = 12-bit code ≈ 2.4 V @ 5.0 V (safety limit) |
| `WATCHDOG_TIMEOUT_MS` | `500U` | allowed CAN silence before shutdown |
| `MCP4725 write addrs` | `0x60<<1, 0x61<<1, 0x62<<1` | 3 candidate 7-bit addresses (write form) probed in order — see §9.4 |

`DAC_MIN_VAL/MAX_VAL` arithmetic: for a 5.0 V reference, code `n` → `n/4096 × 5 V`
(0.8 V ≈ 655, 2.4 V ≈ 1966). Codes below min / above max are clamped to keep the
downstream (motor controller) throttle input inside a safe window.

### NVIC (`.ioc` + `main.c`)

Priority group 4 (all 4 bits preempt). The `.ioc` shows only system exceptions at
priority 0 (NMI, HardFault, MemManage, BusFault, UsageFault, SVCall, PendSV, DebugMon)
and SysTick at priority 15. The only peripheral interrupt enabled in code is
**FDCAN1_IT0 at preempt priority (0,0)**; all other peripheral IRQ vectors stay at the
weak `Default_Handler`. `HAL_MspInit` (in `stm32g4xx_hal_msp.c`) enables SYSCFG + PWR
clocks (`__HAL_RCC_SYSCFG_CLK_ENABLE`, `__HAL_RCC_PWR_CLK_ENABLE`), and voltage scaling
uses `PWR_REGULATOR_VOLTAGE_SCALE1`.

---

## 6. GPIO pin map (full usage, from `main.c`/`HAL_FDCAN_MspInit`)

All GPIO is configured **in code** (`MX_GPIO_Init`, `HAL_FDCAN_MspInit`);
the CubeMX `.ioc` does not declare pins (only RCC+NVIC IPs), so this table is the
single source of truth for wiring.

| Pin | Function | Mode | Pull | Speed | Logic / polarity |
|---|---|---|---|---|---|
| PA0 | Mode Reverse relay driver | Output push-pull | none | Low | **Active-low**: RESET=RELAY ON, SET=OFF |
| PA1 | — | (free) | — | — | unused |
| PA2 | Mode Drive relay driver | Output push-pull | none | Low | active-low, idem |
| PA3 | — | (free) | — | — | unused |
| PA4 | Ignition relay driver | Output push-pull | none | Low | active-low, idem |
| PA5 | SW-I2C SCL → MCP4725 | Output open-drain | pull-up | High | idle HIGH; clock line |
| PA6 | — | (free) | — | — | unused |
| PA7 | SW-I2C SDA → MCP4725 | Output open-drain | pull-up | High | idle HIGH; data line; ACK sampled low |
| PA9 | — | (free) | — | — | unused |
| PA11 | **FDCAN1_RX** | AF9 push-pull | none | Very High | CAN differential input side |
| PA12 | **FDCAN1_TX** | AF9 push-pull | none | Very High | CAN output to transceiver TXD (node never transmits) |
| PC6 | Status LED | Output push-pull | none | Low | **active-low** ("High = OFF"); toggled per 0x0BB value change |
| PC13..PC15 | — | (free) | — | — | unused |

Reset state written before configuring each pin:

- PA0/PA2/PA4 → `GPIO_PIN_SET` (= relays OFF on power-up / reset).
- PA5/PA7 → `GPIO_PIN_SET` (I2C idle high).
- PC6 → `GPIO_PIN_SET` (LED off).

Clocks: GPIOA and GPIOC enabled in `MX_GPIO_Init`; GPIOA re-enabled in FDCAN MSP init.
On the UFQFPN48 (STM32G431CBU6) package PA11 = FDCAN1_RX and PA12 = FDCAN1_TX via
alternate function AF9. Only PA0/PA2/PA4/PA5/PA7/PC6 are outputs besides the two CAN pins;
PA13 (SWDIO) / PA14 (SWCLK) are not configured in code → debugger default.
All other pins stay as their default input state and are not driven.

### Relay-driver summary (the "state machine" outputs)

Decode logic (`main.c:283-303`): `mode = rxData[0] & 0x0F`. **Ignition is ON in every
non-zero mode (Park/Drive/Reverse); Drive and Reverse are mutually exclusive.**
`0x03`=Park (IGN on, Drive off, Rev off); `0x05`=Drive (IGN on, Drive on, Rev off);
`0x09`=Reverse (IGN on, Drive off, Rev on). Every other value (including `0x00` and any
non-zero unused nibble) = all relays off. The three pins are always written together in
one of the 4 relay states shown below — they are never independently driven in the
current file (unlike the older `.bak` code, which used per-bit masks).

| Byte `[0]` masked & 0x0F | Meaning | IGN (PA4) | DRIVE (PA2) | REV (PA0) |
|---|---|---|---|---|
| 0x00 | OFF / safe default | SET (off) | SET (off) | SET (off) |
| 0x03 | Park | RESET (on) | SET (off) | SET (off) |
| 0x05 | Drive | RESET (on) | RESET (on) | SET (off) |
| 0x09 | Reverse | RESET (on) | SET (off) | RESET (on) |
| any other 0x0F nibble | safe default | SET (off) | SET (off) | SET (off) |

---

## 7. FDCAN1 configuration (Rx side)

Configured in `MX_FDCAN1_Init()`:

| Parameter | Value |
|---|---|
| Frame format | `FDCAN_FRAME_CLASSIC` (classic CAN, 11-bit ID) |
| Mode | `FDCAN_MODE_NORMAL` |
| AutoRetransmission | ENABLE |
| TransmitPause | DISABLE |
| ProtocolException | DISABLE |
| NominalPrescaler | 2 |
| NominalSyncJumpWidth | 2 |
| NominalTimeSeg1 | 13 |
| NominalTimeSeg2 | 2 |
| DataPrescaler | 1 (FD fields unused) |
| DataSyncJumpWidth | 1 |
| DataTimeSeg1 | 1 |
| DataTimeSeg2 | 1 |
| StdFiltersNbr | 2 |
| ExtFiltersNbr | 0 |
| TxFifoQueueMode | FDCAN_TX_FIFO_OPERATION |

**Baud derivation**: kernel = PCLK1 = 16 MHz, prescaler 2 → Tq clock 8 MHz → 125 ns/Tq.
Total nominal Tq per bit = 1 (sync) + 13 (TSEG1) + 2 (TSEG2) = 16 → bit time = 16 × 125 ns
= 2 µs → **500 kbps**. Sample point = (1+13)/16 = **87.5 %**.

### Rx filters (both to FIFO0)

| Filter idx | Type | ID1 | ID2 | Effect |
|---|---|---|---|---|
| 0 | mask | `0x0BB` | `0x7FF` | exact match only 0x0BB (Relays/Vehicle state) |
| 1 | mask | `0x0AA` | `0x7FF` | exact match only 0x0AA (Throttle) |

All other IDs are rejected in hardware. Because `IdType = FDCAN_STANDARD_ID`, both filters
are written by the HAL into the **standard-ID filter element list** in FDCAN message RAM
(`msgRam.StandardFilterSA + FilterIndex`, offsets 0 and 1), and `FDCAN_FILTER_TO_RXFIFO0`
steers matches to RX FIFO 0. No `FDCAN_RXFIFO0` overrun/other notifications are enabled.

### MSP (in `main.c`)

- FDCAN clock source: `RCC_PERIPHCLK_FDCAN` + `RCC_FDCANCLKSOURCE_PCLK1` via
  `HAL_RCCEx_PeriphCLKConfig`.
- Enables `__HAL_RCC_FDCAN_CLK_ENABLE()` and GPIOA clock.
- PA11/PA12 as `GPIO_AF9_FDCAN1`, push-pull, no pull, very-high speed.
- NVIC: `FDCAN1_IT0_IRQn` priority (0,0), enabled.

### Interrupt path

1. Frame arrives → hardware → `FDCAN1_IT0_IRQn` →
2. `FDCAN1_IT0_IRQHandler()` in `stm32g4xx_it.c` (USER CODE 1) →
3. `HAL_FDCAN_IRQHandler(&hfdcan1)` →
4. RX FIFO0 new-message callback `HAL_FDCAN_RxFifo0Callback()` in `main.c`.

---

## 8. Application globals (`main.c`)

| Symbol | Type | Role |
|---|---|---|
| `hfdcan1` | `FDCAN_HandleTypeDef` | FDCAN1 handle (not volatile) |
| `analog_value` | `uint16_t volatile` | 16-bit big-endian throttle from 0x0AA `[0],[1]` |
| `current_mode` | `uint8_t volatile` | last full 0x0BB byte (mode + brake bit) |
| `last_rx_time` | `uint32_t volatile` | `HAL_GetTick()` at last accepted frame |
| `first_msg_received` | `uint8_t volatile` | 1 after first 0x0AA/0x0BB |
| `dac_update_needed` | `uint8_t volatile` | 1 → main loop must (re)write DAC |

Shared with ISR: `current_mode`, `analog_value`, `last_rx_time`,
`first_msg_received`, `dac_update_needed` (all read/written from IRQ + main; flag handshake
`dac_update_needed` prevents DAC blocking calls inside the ISR).

---

## 9. Line-by-line behaviour of the firmware

### 9.1 `main()` startup sequence

1. `HAL_Init()` — resets peripherals, sets SysTick, NVIC group, MSP init.
2. **DWT cycle counter** is enabled but **never read**: `CoreDebug->DEMCR |= TRCENA_Msk`;
   the DWT LAR at `0xE0001FB0` is unlocked with `0xC5ACCE55`; `DWT->CTRL |= CYCCNTENA`.
   No µs/delay code uses CYCCNT — the bit-bang delays are plain NOP loops (see §9.4).
3. `SystemClock_Config()` — 16 MHz HSI (see §3).
4. `MX_GPIO_Init()` — relay + LED + I2C pins (§6).
5. `MX_FDCAN1_Init()` — handle init + 2 filters (§7).
6. `HAL_FDCAN_Start()` → error → `Error_Handler`.
7. `HAL_FDCAN_ActivateNotification(FDCAN_IT_RX_FIFO0_NEW_MESSAGE)` → enables
   new-message interrupts.
8. **Main loop** (runs forever):

```c
while (1) {
  if (first_msg_received && (HAL_GetTick() - last_rx_time) > WATCHDOG_TIMEOUT_MS) {
      HAL_GPIO_WritePin(IGN,   SET);  // ignition off
      HAL_GPIO_WritePin(DRIVE, SET);  // drive off
      HAL_GPIO_WritePin(REV,   SET);  // reverse off
      writeMCP4725(0); current_mode = 0x00;
  } else if (dac_update_needed) {
      dac_update_needed = 0;
      mode = current_mode & 0x0F;
      if (mode == 0x00 || analog_value == 0) {
          writeMCP4725(0);                     // both zero → 0 V
      } else {
          dac_12bit = (analog_value + 8) >> 4; // 16-bit → 12-bit w/ round
          if (dac_12bit < DAC_MIN_VAL) dac_12bit = DAC_MIN_VAL;
          else if (dac_12bit > DAC_MAX_VAL) dac_12bit = DAC_MAX_VAL;
          writeMCP4725(dac_12bit);
      }
  }
  HAL_Delay(5);
}
```

Key decisions encoded here:
- **Watchdog**: if no valid frame for >500 ms since the first one, all relays OFF
  (pins SET), DAC = 0 V, mode cleared. The DAC reset level is 0 V — different from `.bak`
  whose main-loop watchdog wrote `DAC_MIN_VAL` (~0.8 V idle).
- **DAC gating**: output is 0 V whenever the stored mode low-nibble is `0x00` **OR** the
  raw throttle word is `0`. Otherwise the 12-bit code = `(analog_value + 8) >> 4`, then
  **clamped to [DAC_MIN_VAL=655, DAC_MAX_VAL=1966]** (≈0.8–2.4 V @ 5.0 V). Consequence:
  any throttle that produces a code below 655 (i.e. `analog_value` in 1…10471) is raised
  to the 0.8 V floor, so only `analog_value == 0` (or mode 0x00) yields a true 0 V output.
  That means the throttle floor is 0.8 V for any non-zero demand in a non-zero gear.
- The DAC write is deferred out of the ISR to the main loop via `dac_update_needed`.
- 5 ms loop period (`HAL_Delay(5)`), watchdog granularity ≈ 5 ms.

### 9.2 `HAL_FDCAN_RxFifo0Callback()` — receive logic

Fires inside IRQ context on every new FIFO0 message:

1. Guard on `FDCAN_IT_RX_FIFO0_NEW_MESSAGE`; reads header + 8 data bytes
   (`HAL_FDCAN_GetRxMessage` FIFO0).
2. **Comm watchdog feed**: if ID is `0x0BB` **or** `0x0AA` → set `last_rx_time`,
   `first_msg_received = 1`. (Only these two IDs can pass the filters.)
3. If ID == `0x0BB` (Relays / Vehicle state):
   - `b = rxData[0]`; the **entire byte** is stored in `current_mode` (brake flag kept,
     gating uses only the low nibble later).
   - `mode = b & 0x0F` — masks off the brake flag bit 4 (`0x10`).
   - **LED**: `static uint8_t last_b = 0xFFU` persists across calls; on every byte
     change `last_b` is updated and PC6 is **toggled** (`HAL_GPIO_TogglePin`) — the LED
     is not on/off with a value, it flips each time the 0x0BB byte changes.
   - Mode decode drives PA4/PA2/PA0 per the table in §6 (exact-match `else if` chains
     for `0x03`/`0x05`/`0x09`, every other low nibble → all relays off).
   - Sets `dac_update_needed = 1`.
4. Else if ID == `0x0AA` (Throttle):
   - `analog_value = ((uint16_t)rxData[0] << 8) | rxData[1]` — big-endian 16-bit.
   - Sets `dac_update_needed = 1`. (DAC scaling/clamping/gating deferred to main loop.)

Note: the two `if`/`else if` branches are **mutually exclusive** (0x0BB handled first,
0x0AA second). Because the filters accept only these two IDs, the watchdog feed test is
always true for any frame that reaches the callback.

### 9.3 Error and fault handling

- `Error_Handler()`: disables IRQs, spins forever. Used for any HAL init failure.
- Fault handlers (`NMI`, `HardFault`, `MemManage`, `BusFault`, `UsageFault`) are the
  CubeMX defaults in `stm32g4xx_it.c`: infinite loops.
- `SysTick_Handler`: only `HAL_IncTick()`.
- No hardware watchdog (IWDG) is configured — the "watchdog" is the soft CAN
  timeout in the main loop only.

### 9.4 Software I2C (bit-bang) — `sw_i2c_*` + `writeMCP4725`

Open-drain PA5/PA7 with internal pull-ups; external pull-ups on the PCB are required for
correct open-drain operation (the internal ones are weak).

- `sw_i2c_delay()` (main.c:326): tight loop of 40 iterations of `volatile` increment + NOP,
  executed between every I2C edge. The actual per-edge delay is **toolchain-dependent**
  (HAL GPIO call overhead + loop); at 16 MHz and a few tens of cycles per edge it lands
  well under the 100 kHz I2C spec (roughly single-digit-µs class). No µs counter is used.
- `sw_i2c_start()` (main.c:332): SDA→1, SCL→1, **then SDA→0 while SCL is high**, then
  SCL→0 → valid START.
- `sw_i2c_stop()` (main.c:343): SDA→0 (SCL already low), SCL→1, then SDA→1 while SCL is
  high → valid STOP.
- `sw_i2c_write_byte(byte)`: shifts MSB-first; each bit sets SDA, pulses SCL high then
  low. After the 8 data bits it **releases SDA** (writes `GPIO_PIN_SET` on an open-drain
  pin → tri-state, letting the slave pull it low), then raises SCL and **samples SDA
  while SCL is high** — returns `1` if the pin reads low (ACK) else `0`; finally drops
  SCL.
- `writeMCP4725(value)` (main.c:386):
  1. Clamp value to 4095 (12-bit).
  2. Probe **three** candidate 7-bit addresses in order, each as the write-form byte
     (addr `<<1`): `0x60`, `0x61`, `0x62`. (The `.bak` source comments explain why:
     A0 pin to GND / A0 pin to VCC / alternative address — the three are probed because
     the fitted module's address pins are not assumed.)
  3. For each candidate: START → write address byte → **regardless of ACK** continue to
     write the full frame — command byte `0x40` (fast write `010`, PD1=0, PD0=0 → normal
     mode, no power-down) → high byte `(value >> 4)` → low nibble `((value << 4) & 0xF0)`
     → STOP.
  4. If the address byte was ACKed, `return` immediately (device found & written); else
     try next address. When no device ACKs, all three probes are still sent before the
     function returns.
  - Net effect: the same voltage is written to **every MCP4725 that ACKs on the bus** —
    a 3-way address probe lets the node drive up to three DACs at once if multiple are
    fitted at 0x60/0x61/0x62, each on the shared SDA/SCL.

### 9.5 Interrupt service / vector summary (`stm32g4xx_it.c` + startup)

- `FDCAN1_IT0_IRQHandler` — only application IRQ, preempt prio 0, forwards to
  `HAL_FDCAN_IRQHandler`.
- Vector table in `startup_stm32g431cbux.s` lists all STM32G431 handlers as weak →
  `Default_Handler` infinite loop; code defines NMI/HardFault/MemManage/BusFault/
  UsageFault/SVC/DebugMon/PendSV/SysTick in `stm32g4xx_it.c`.
- `FDCAN1_IT0_IRQHandler` is installed (vector index 37 in the startup table, after
  USB_LP) but `FDCAN1_IT1` is left at the weak `Default_Handler`. FDCAN1 line-0
  interrupts carry the Rx FIFO0 new-message events used here.

---

## 10. Protocol specification (ESP32 ⇄ STM32)

Transport: classic CAN 2.0A, 11-bit standard IDs, 8 data bytes, 500 kbps,
sample point ~87.5 %.

### Message 0x0BB — Vehicle State / Relay Command (rate = bus master's tick)

| Byte | Bit | Meaning |
|---|---|---|
| data[0] | 3:0 | gear/ignition command: `0x00` OFF, `0x03` Park, `0x05` Drive, `0x09` Reverse |
| data[0] | 4 (0x10) | brake flag — stored whole in `current_mode`, masked off for relay decode (informational; **not** used to force a state) |
| data[1..7] | — | don't-care (not parsed) |

### Message 0x0AA — Drive-by-wire Throttle

| Byte | Meaning |
|---|---|
| data[0] | throttle high byte (MSB) |
| data[1] | throttle low byte (LSB) |
| data[2..7] | don't-care |

`analog_value` ∈ [0, 65535]. Conversion to DAC code: `(val + 8) >> 4` (rounding) ∈
[0, 4096]; values in [655, 1966] map 1:1 to ≈0.8–2.4 V, anything below the floor is
clamped up to 655 and anything above the ceiling is clamped down to 1966 (so 0 V is only
reachable through the zeroing rules below). Output is forced to 0 V when the stored mode
low nibble is 0x00 **or** `analog_value == 0`. (`writeMCP4725` itself also clamps any
passed value to ≤4095.)

### Watchdog

Armed after first accepted 0x0AA/0x0BB. If nothing arrives for 500 ms → relays open,
DAC = 0 V. Frames must arrive at least every ~500 ms to hold outputs.

---

## 11. Behaviour delta vs `main.c.bak` (intentional, current file is authoritative)

| Aspect | `.bak` (older) | `main.c` (current) |
|---|---|---|
| DAC source | written directly in ISR on 0x0AA | deferred via `dac_update_needed` to main loop |
| DAC write | from 0x0AA always, gated by nothing (relays "physically prevent" motion) | from main loop, **gated by stored mode** (DAC=0 unless mode≠0 and throttle≠0) |
| Watchdog DAC value | `writeMCP4725(DAC_MIN_VAL)` (~0.8 V idle) | `writeMCP4725(0)` (0 V) |
| Watchdog write | main loop calls `writeMCP4725(DAC_MIN_VAL)` on timeout; 0x0AA DAC write is in ISR | main loop calls `writeMCP4725(0)` on timeout; all DAC writes from main loop |
| `last_b` LED toggle | same | same |
| Relay decode | bitmask: bit0=IGN, bit1=Drive, bit2=Rev (each byte bit) | value-based decode of low nibble: 0x03/0x05/0x09 chains |
| Loop delay | `HAL_Delay(10)` | `HAL_Delay(5)` |
| CAN frame handling order | 0x0AA then 0x0BB | 0x0BB then 0x0AA |
| Filters | 0x0AA → idx0, 0x0BB → idx1 | 0x0BB → idx0, 0x0AA → idx1 |
| `.bak` line-count | 426 | 406 |

`main.c.bak` is not part of the build (not in `Debug/objects.list`) and is retained
only as reference.

---

## 12. Threading / concurrency model

Single foreground + one ISR:

- **IRQ (FDCAN1_IT0, prio 0)**: decodes the frame and drives the relay pins directly
  (fast HAL GPIO writes, safe from IRQ context). It never calls `writeMCP4725` (bit-bang
  would be slow and unsafe under interrupt); instead it only sets `analog_value` /
  `current_mode` and raises `dac_update_needed`.
- **Main loop**: consumes `dac_update_needed` and performs the DAC I2C transaction,
  runs the soft comms watchdog, `HAL_Delay(5)`.
- Volatile sharing is race-tolerant for this design: `analog_value` is written only by
  the ISR (0x0AA); `dac_update_needed` is set by the ISR (0x0AA/0x0BB) and cleared by the
  main loop; `current_mode` is written by the ISR (0x0BB) and by the main-loop watchdog
  (reset to 0x00). DAC writes happen only in the main loop. A frame arriving mid-update
  merely makes the loop write the newest values one tick later.
- Note: `analog_value` and `current_mode` are consumed together only by the main loop
  (never inside the ISR), and each new frame overwrites `current_mode`/`analog_value`
  before re-raising the flag — no torn use of the pair can occur at the DAC writer.

---

## 13. Safety design notes

- Active-low relay drive + power-up pins SET → relays **de-energized** before any CAN
  traffic and on any reset.
- DAC command byte `0x40` = normal write (no power-down). When a throttle is output, the
  clamp bounds the 12-bit code to [655, 1966] ≈ 0.8–2.4 V at VDD=5.0 V (a safe window for
  the downstream motor controller). 0 V is written only for the mode==0x00 / throttle==0
  cases and on watchdog timeout.
- Soft comms watchdog (500 ms) forces all-off + 0 V on link loss.
- Unknown modes / 0x00 → all relays off (fail-safe branch).
- No hardware IWDG, and no application Tx path exists — FDCAN TX is configured
  (`FDCAN1_TX`/PA12 AF9, TX FIFO mode) but the code never calls `HAL_FDCAN_AddMessageToFifo`.
  The node is effectively receive-only on the bus.

---

## 14. Board wiring checklist (derived from §6)

1. **CAN**: FDCAN1_RX = PA11, FDCAN1_TX = PA12 (both AF9, very-high speed). PA11 goes to
   the transceiver's RXD; PA12 drives the transceiver's TXD (a CAN transceiver is required
   even though the firmware never transmits).
2. **Relay drivers** (active-low, e.g. via NPN/MOSFET + relay coils): PA4→IGN,
   PA2→Drive, PA0→Reverse; energized when pin is LOW.
3. **MCP4725**: SCL=PA5, SDA=PA7; address pins set so device answers at 0x60/0x61/0x62;
   VDD=5.0 V reference; VOUT feeds motor/throttle input; pull-ups on SDA/SCL.
4. **Status LED** on PC6, active-low (cathode/board "off when high"); blinks/toggles on
   each 0x0BB value change.

---

## 15. Rebuild / flash

- STM32CubeIDE managed GNU Make build; output `Debug/VCU_2.elf` + `.map` + `.list`.
- Link script referenced from the original workspace
  (`.../workspace_1.19.0/VCU_2/STM32G431CBUX_FLASH.ld` in `Debug/makefile`) — if moved,
  re-point the linker script in CubeIDE.
- Regeneration flow: CubeMX edits to `VCU_2.ioc` regenerate only `main.c`/`main.h`/
  `stm32g4xx_it.c`/`stm32g4xx_hal_msp.c`/`stm32g4xx_hal_conf.h` (see `.mxproject`);
  pin code lives in user sections and would be overwritten by re-gen → keep pin setup
  comments' USER CODE regions intact.
