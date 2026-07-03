# ESP32-S3 Microcontroller — Technical Reference

## Table of Contents

1. [Overview](#overview)
2. [Core Architecture & Processing](#core-architecture--processing)
3. [Family Overview & Variant Matrix](#family-overview--variant-matrix)
4. [Memory Hierarchy & Subsystem Constraints](#memory-hierarchy--subsystem-constraints)
5. [Pinout, Pin-Mux & Boot Configuration](#pinout-pin-mux--boot-configuration)
6. [Electrical Characteristics, Clocks & Power](#electrical-characteristics-clocks--power)
7. [Power Delivery & Brownout Mitigation](#power-delivery--brownout-mitigation)
8. [Analog Subsystem (ADC)](#analog-subsystem-adc)
9. [Capacitive Touch Sensor](#capacitive-touch-sensor)
10. [USB Subsystem](#usb-subsystem)
11. [Peripherals & Protocol Limits](#peripherals--protocol-limits)
12. [PCB Design, Layout & Bring-Up](#pcb-design-layout--bring-up)
13. [Silicon Errata](#silicon-errata)
14. [Migration Notes & Community Experience](#migration-notes--community-experience)
15. [Design Recommendations](#design-recommendations)
16. [Key References](#key-references)

---

## Overview

The ESP32-S3 is Espressif's Xtensa LX7-based Wi‑Fi 4 and Bluetooth 5.0 LE SoC line aimed at AIoT, HMI, camera/LCD, USB, and low-power edge applications. It combines a dual-core 32-bit Xtensa LX7 microprocessor at up to 240 MHz, 45 physical GPIOs, a flexible GPIO matrix, native full-speed USB OTG plus fixed-function USB Serial/JTAG, up to 512 KB on-chip SRAM, 384 KB ROM, 16 KB RTC SRAM, optional in-package flash and/or PSRAM, and two ULP coprocessors (RISC‑V and FSM).

The defining architectural addition over previous ESP32 generations is the **Processor Instruction Extensions (PIE)** — a proprietary set of SIMD vector instructions for neural network acceleration and DSP workloads. With these extensions, the S3 achieves approximately 8× AI inference throughput compared to the legacy ESP32 LX6 architecture.

**Key design constraints** that differentiate the S3 from ESP32 and ESP32-S2:

- Strapping pins (GPIO0, GPIO3, GPIO45, GPIO46) are not free during reset.
- GPIO19/20 are dedicated USB Serial/JTAG pins by default — not ordinary GPIOs.
- GPIO26–GPIO32 are normally reserved for flash/PSRAM (SPI0/1 bus).
- GPIO33–GPIO37 are additionally reserved on octal-memory variants (R8, R8V).
- ADC2 has a documented digital-controller erratum; prefer ADC1 for new designs.
- High-speed flash/PSRAM modes (120 MHz DDR) are temperature-sensitive.
- The S3 has **no DAC peripheral** — do not assume parity with the original ESP32.

---

## Core Architecture & Processing

### Xtensa LX7 CPU

The primary processing engine is a dual-core Xtensa LX7 microprocessor with a five-stage pipeline, a 128-bit data bus, and a dedicated Single Precision Floating Point Unit (FPU). At 240 MHz, the SoC achieves a CoreMark score of 1181.60 (4.92 CoreMark/MHz).

### Processor Instruction Extensions (PIE)

The PIE architecture is based on the Tensilica Instruction Extension (TIE) architecture and is engineered for high-density mathematical operations underpinning neural network inference (TensorFlow Lite, ESP-WHO). The instruction set uses:

- Eight 128-bit wide vector registers (QR0–QR7)
- Two 160-bit accumulators (QACC_H, QACC_L)

During MAC (multiplication-accumulation) operations — the fundamental building block of convolutional neural network layers — the PIE unit processes **sixteen 8-bit** or **eight 16-bit** MAC results in parallel. Instructions integrate data transfer seamlessly with arithmetic execution, allowing non-aligned 128-bit vector data loads/stores in a single processor cycle.

Using PIE through inline assembly (`ee`-prefixed instructions) or optimized C libraries yields approximately 8× throughput increase in AI inference compared to legacy ESP32 LX6.

### Execution Speed Optimization

Empirical benchmarking reveals that execution speed depends on a matrix of compiler optimizations, flash SPI modes, and cache configuration. A counter-intuitive result: compiling with `-Os` (size optimization) often yields **faster** execution than `-O2` (speed optimization), because smaller code fits more efficiently into the instruction cache, reducing costly cache-miss penalties and external flash fetches.

| Configuration | CPU Freq | Optimization | SPI Mode | Relative Speed |
|---|---|---|---|---|
| Baseline | 160 MHz | Default | DIO | 100% (baseline) |
| Frequency Max | 240 MHz | Default | DIO | 67.7% of baseline time |
| Size Optimized | 240 MHz | `-Os` | DIO | 61.2% |
| Speed Optimized | 240 MHz | `-O2` | DIO | 67.5% |
| Maximum Bandwidth | 240 MHz | `-Os` | QIO + Max Cache | 59.2% |

For maximum throughput: combine 240 MHz clock, `-Os` compiler flag, QIO flash mode, and maximum instruction/data cache sizes in menuconfig.

### Coprocessor & Low-Power Subsystem

The ULP subsystem contains two distinct coprocessors operating in the RTC power domain, active during deep sleep:

- **ULP-RISC‑V**: RV32IMC, 32 general-purpose registers, multiply/divide, interrupts. Accesses RTC slow memory and RTC-domain peripherals (RTC_CNTL, RTC_IO, SARADC).
- **ULP-FSM**: Legacy finite-state machine for low-power sensor activity.

Both can independently poll sensors via I²C, perform ADC measurements, or monitor capacitive touch pads, waking the primary LX7 cores only when thresholds are met. Deep-sleep current with ULP-FSM: 170 µA typ; with ULP-RISC‑V: 190 µA typ.

---

## Family Overview & Variant Matrix

Architectural constants across the family: dual-core Xtensa LX7 up to 240 MHz, Wi‑Fi 802.11 b/g/n, Bluetooth 5 LE, vector instructions, 45 GPIOs, 512 KB SRAM, 384 KB ROM, 16 KB RTC SRAM, ULP subsystem.

| Variant | Embedded Flash | Embedded PSRAM | Memory Bus | Practical Implication |
|---|---|---|---|---|
| ESP32-S3 | None | None | External flash required; ext. PSRAM optional | Max layout flexibility; full MSPI design burden |
| ESP32-S3R2 / RH2 | None | 2 MB quad | In-package quad PSRAM | External flash still required |
| ESP32-S3R8 | None | 8 MB octal | In-package octal PSRAM | GPIO33–GPIO37 become PSRAM-reserved |
| ESP32-S3R8V / R16V | None | 8/16 MB octal | Octal PSRAM at 1.8 V VDD_SPI | GPIO33–GPIO37 + SPICLK_P/N in 1.8 V domain |
| ESP32-S3FN8 | 8 MB quad | None | In-package quad flash | No embedded PSRAM |
| ESP32-S3FH4R2 | 4 MB quad | 2 MB quad | Quad flash + quad PSRAM | Compact SiP option |

**Critical distinction**: Modules with the **V suffix** (e.g., ESP32-S3R8V) operate flash/PSRAM on a **1.8 V** domain rather than 3.3 V. This requires strict voltage matching — failure to configure VDD_SPI correctly **will permanently destroy** the in-package memory.

Module nomenclature example: `ESP32-S3-WROOM-1U-N8R8` = external antenna connector (1U), 8 MB Flash (N8), 8 MB PSRAM (R8).

---

## Memory Hierarchy & Subsystem Constraints

### Internal Memory

| Memory | Size | Notes |
|---|---|---|
| SRAM | 512 KB | Shared between IRAM and DRAM; allocation is configurable |
| ROM | 384 KB | Bootloader and fixed firmware |
| RTC SRAM | 16 KB | Retained in deep sleep; accessible by ULP coprocessors |

Unused internal SRAM can be reassigned between IRAM and DRAM. The maximum static DRAM available to an application is reduced by internal SRAM consumed for instruction storage.

### External PSRAM & Flash

ESP32-S3 supports SPI, Dual SPI, Quad SPI (QSPI), and Octal SPI (OPI) interfaces for external memory. Quad flash supports STR mode only; octal flash may support STR or DTR; quad PSRAM supports STR only; octal PSRAM supports **DTR only**.

**Shared-clock rule**: Flash and PSRAM share the same internal clock (SPI0/1 bus). Flash/PSRAM mode selection is a system decision, not two independent menu choices.

### Memory Management Unit (MMU) Limitations

While the hardware supports physical PSRAM chips up to 32 MB, the internal MMU imposes a hard cap of **32 MB virtual address space** shared between flash instructions, read-only data, and external PSRAM. Within standard firmware environments (ESP-IDF, Zephyr), the contiguous data heap mapped to PSRAM is traditionally capped at **8 MB**. A 16 MB or 32 MB PSRAM chip cannot be treated as a single block of RAM — manual bank switching or discrete region mapping is required. Single buffers larger than the MMU window will fail allocation.

External RAM can be integrated into the memory map, added to the capability allocator, made the default `malloc()` backing store, or used for `.bss`, `.noinit`, and XiP from PSRAM in supported configurations. Code execution from PSRAM (`execute_from_psram`) frees IRAM space and prevents execution halts during flash write operations (e.g., OTA updates).

### DMA Buffer Requirements

Most peripheral DMA engines require **DRAM-resident, word-aligned buffers**. Use static `DMA_ATTR` buffers or heap allocation with `MALLOC_CAP_DMA`. DMA buffers on stacks that may live in PSRAM **must not be used** — this is a classic "it compiles, it runs, then it randomly corrupts" migration trap.

### Thermal Instability at High Clock Speeds

When using Octal PSRAM in DDR mode at 80 MHz or 120 MHz, the memory interface becomes sensitive to temperature fluctuations. A ~20 °C delta from power-on temperature can cause microscopic timing skews on the high-speed bus, resulting in random memory corruption, access violations, and hard system crashes. Espressif marks 120 MHz DDR as **experimental**. Mitigation: limit PSRAM clock to 80 MHz (DDR) or operate strictly in STR mode. ESP-IDF provides a temperature-based dynamic phase-adjustment option.

### Zephyr RTOS Memory Configuration

For Zephyr RTOS users, PSRAM integration requires explicit Kconfig parameters:

| Kconfig Parameter | Function |
|---|---|
| `ESP_SPIRAM` | Enables external SPI RAM; activates `SHARED_MULTI_HEAP` |
| `SPIRAM_MODE` | Set to `SPIRAM_MODE_OCT` for Octal capability |
| `SPIRAM_FETCH_INSTRUCTIONS` | Move executable instructions from flash to PSRAM at startup |
| `SPIRAM_RODATA` | Move read-only data from flash to PSRAM |
| `SPIRAM_SPEED` | Clock speed: `SPIRAM_SPEED_20M` through `SPIRAM_SPEED_120M` |

The device tree must accurately define the PSRAM address block (e.g., `<0x3c000000 DT_SIZE_M(8)>`) to prevent hard faults from the kernel spanning the heap across invalid physical addresses.

### Flash/PSRAM Voltage Configuration (Safety-Critical)

1.8 V PSRAM and flash must match each other. For 1.8 V parts, you must either strap **GPIO45 high** on boot or burn the `VDD_SPI_FORCE` eFuse. **Failing to do so can permanently damage the PSRAM and/or flash.** Production devices should use `espefuse set-flash-voltage [1.8V | 3.3V]` to permanently override GPIO45, forcing the correct regulator output regardless of external pin state.

---

## Pinout, Pin-Mux & Boot Configuration

### Physical Package Pinout (QFN56)

| Edge | Pins | Signals |
|---|---|---|
| Left (1–14) | 1–14 | LNA_IN, VDD3P3, VDD3P3, CHIP_PU, GPIO0–GPIO9 |
| Bottom (15–28) | 15–28 | GPIO10–GPIO14, VDD3P3_RTC, XTAL_32K_P/N, GPIO17–GPIO21, SPICS1 |
| Right (29–42) | 29–42 | VDD_SPI, SPIHD, SPIWP, SPICS0, SPICLK, SPIQ, SPID, SPICLK_N/P, GPIO33–GPIO37 |
| Top (43–56) | 43–56 | GPIO38, MTCK, MTDO, VDD3P3_CPU, MTDI, MTMS, U0TXD, U0RXD, GPIO45–GPIO46, XTAL_N/P, VDDA, VDDA |
| Pad | 57 | GND |

### Logical GPIO Map

The ESP32-S3 exposes 45 physical GPIOs (GPIO0–GPIO21, GPIO26–GPIO48). Peripheral inputs can be taken from any GPIO and outputs routed to any GPIO via the IO MUX, RTC IO MUX, and GPIO matrix — but this freedom is constrained by four categories:

| GPIO Range | Primary Functions | Design Note |
|---|---|---|
| GPIO0 | RTC_GPIO0, strapping (boot mode) | Keep boot-safe; weak pull-up default |
| GPIO1–GPIO10 | RTC_GPIO1–10, ADC1_CH0–9 | Best analog bank; use for new designs |
| GPIO11–GPIO20 | RTC_GPIO11–20, ADC2_CH0–9 | GPIO19/20 = USB Serial/JTAG by default; ADC2 has errata |
| GPIO21 | RTC_GPIO21 | General RTC-capable pin |
| GPIO26–GPIO32 | SPI0/1 memory bus | **Reserve for flash/PSRAM** |
| GPIO33–GPIO37 | SPIIO4–SPIIO7, SPIDQS (octal only) | **Strictly prohibited** on R8/R8V modules |
| GPIO38 | General GPIO | Usually safe |
| GPIO39–GPIO42 | JTAG (MTCK, MTDO, MTDI, MTMS) | Can be repurposed; watch debug needs |
| GPIO43–GPIO44 | UART0 TX/RX defaults | Often used for boot logs / serial console |
| GPIO45 | Strapping (VDD_SPI voltage) | Keep boot-safe |
| GPIO46 | Strapping (ROM messages) | Keep boot-safe |
| GPIO47–GPIO48 | General GPIO | Usually safe |

### IOMUX vs. GPIO Matrix

The routing architecture offers two paths:

1. **IO_MUX (Priority 1)**: Fixed, direct connections. Lowest latency, highest frequency capability. Required for signals above ~40 MHz.
2. **GPIO Matrix (Priority 2)**: Flexible routing of any peripheral to any pin. Capped at **40 MHz** (or 80 MHz in some cases). If any signal of an SPI host passes through the matrix, **all** signals are routed through it.

For high-speed SPI (e.g., 120 MHz LCD), use the dedicated IO_MUX pins: SPI2 CS0 = GPIO10, SCLK = GPIO12, MISO = GPIO13, MOSI = GPIO11, WP = GPIO14, HD = GPIO9.

**Priority model**: Priority 1 = fixed IO_MUX pins; Priority 2 = GPIO-matrix-routed (freely usable); Priority 3 = usable with caution (strapping/USB/JTAG/UART0 conflicts); Priority 4 = flash/PSRAM pins (avoid for application I/O).

### Strapping Pins & Boot Modes

Four pins are sampled at power-on reset (after CHIP_PU goes high, held for 3 ms):

| Pin | Function | Default State | Boot Requirement |
|---|---|---|---|
| GPIO0 | Boot mode control | Weak pull-up | HIGH = SPI boot; LOW = download mode. No large capacitors. |
| GPIO3 | JTAG signal control | Floating | Controls JTAG behavior with eFuses |
| GPIO45 | VDD_SPI voltage | Weak pull-down | LOW = 3.3 V flash; HIGH = 1.8 V flash. **Critical for hardware survival.** |
| GPIO46 | ROM message control | Weak pull-down | LOW = enable ROM messages; HIGH = disable |

**Boot mode decision**:
- **SPI Boot** (default): GPIO0 = 1 or GPIO46 = 1 — firmware executes from flash.
- **Joint Download Boot**: GPIO0 = 0 **and** GPIO46 = 0 — enters USB-Serial/JTAG, USB-OTG, or UART download mode.

GPIO45 controls VDD_SPI voltage when eFuse is not forcing the value. On 1.8 V memory modules (V-suffix), accidentally pulling GPIO45 low during boot **will permanently destroy the memory ICs**. Production devices should burn the `VDD_SPI_FORCE` eFuse.

**Bring-up requirements**:
- Place a pull-up resistor on GPIO0.
- Do not add large capacitance on GPIO0 (may force unexpected download mode).
- Use an RC delay on CHIP_PU (10 kΩ + 1 µF typical) so 3.3 V rails stabilize before enable is asserted.
- Strap values must remain valid for 3 ms after CHIP_PU goes high.

### GPIO Boot-Time Glitch States

Several pins exhibit transient voltage glitches at power-up before firmware gains control:

- **GPIO19/20** (USB D-/D+): Emit a ~60 µs low-level or high-level pulse.
- **GPIO39/40** (JTAG MTCK/MTDO): Internal pull-ups activate immediately on reset.

If these pins drive MOSFET gates for motors, relays, or other actuators, the boot-time glitch can cause unintended physical actuation. Mitigation: external pull-down resistors, hardware interlocks with a separate enable line, or burning `EFUSE_DIS_PAD_JTAG` to prevent JTAG pins from being driven during boot.

### Deep Sleep GPIO Retention

Standard digital pads lose state and revert to high-impedance during deep sleep. To hold a pin state (e.g., MOSFET gate), use `gpio_deep_sleep_hold_en()`. This feature is restricted to GPIOs in the **VDD3P3_RTC power domain**; standard digital-only pins cannot be held during deep sleep.

---

## Electrical Characteristics, Clocks & Power

### Absolute Maximum Ratings

- Input power pins: –0.3 V to 3.6 V
- Cumulative I/O output current: 1500 mA
- Recommended operating: 3.0–3.6 V (VDDA, VDD3P3, VDD3P3_RTC, VDD3P3_CPU)
- VDD_SPI input: 1.8 V, 3.3 V, or up to 3.6 V (configuration-dependent)
- For single-supply designs: source capable of ≥500 mA
- When burning eFuses: VDD3P3_CPU ≤ 3.3 V

### DC Characteristics (3.3 V, 25 °C)

- Input capacitance: 2 pF nominal
- VIH: ≥ 0.75 × VDD; VIL: ≤ 0.25 × VDD
- Input leakage: ≤ 50 nA
- Internal pull-up/pull-down: 45 kΩ nominal
- Output current at `PAD_DRIVER = 3`: ~40 mA source, ~28 mA sink (test conditions — not continuous rating)

### VDD_SPI Feed Path

- 3.3 V path through internal ~14 Ω from VDD3P3_RTC
- Internal 1.8 V flash regulator: ~40 mA typical output current

### Clocking

| Source | Frequency | Notes |
|---|---|---|
| Main crystal (XTAL) | 40 MHz | **Required.** FW supports only 40 MHz. ±10 ppm, amplitude >500 mV recommended. |
| Internal fast RC (RC_FAST) | ~17.5 MHz | Approximate; not for RF timing |
| Internal slow RC (RC_SLOW) | ~136 kHz | Not suitable for BLE connection accuracy (requires ≤500 ppm) |
| External 32.768 kHz | 32.768 kHz | Optional for RTC. ESR ≤ 70 kΩ. If unused, pins can be GPIOs. |

**BLE low-power clock constraint**: The BLE sleep clock must be within **500 ppm**. Using the main XTAL keeps the crystal on during light-sleep (higher current). The internal 136 kHz RC oscillator does not meet BLE connection accuracy and is only suitable for legacy advertising/scanning. This is why battery BLE products typically include the external 32.768 kHz crystal.

### Timing Constraints

- Power rails must stabilize for ≥50 µs before CHIP_PU goes high.
- CHIP_PU must be held below reset threshold for ≥50 µs to guarantee reset.
- Strapping pins must remain valid for 3 ms after CHIP_PU goes high.
- Dynamic frequency scaling adds interrupt latency: ~0.2 µs best case, up to ~40 µs worst case (40→80 MHz switch on interrupt entry). Critical for timing-sensitive bit-banging or control loops.

### Current Consumption

| Mode | Condition | Current |
|---|---|---|
| Wi‑Fi TX | 802.11b, 1 Mbps, 21 dBm | 340 mA peak |
| Wi‑Fi TX | 802.11g, 54 Mbps, 19 dBm | 291 mA peak |
| Wi‑Fi TX | 802.11n HT20 MCS7, 18.5 dBm | 283 mA peak |
| Wi‑Fi TX | 802.11n HT40 MCS7, 18 dBm | 286 mA peak |
| Wi‑Fi RX | 802.11b/g/n HT20 | 88 mA peak |
| Wi‑Fi RX | HT40 | 91 mA peak |
| BLE TX | 21 dBm | 335 mA peak |
| BLE TX | –15 dBm | 116 mA peak |
| BLE RX | Receive | 93 mA peak |
| Light-sleep | No PSRAM adder | 240 µA typ |
| Light-sleep | +8 MB octal PSRAM (3.3 V) | ~380 µA typ |
| Light-sleep | +8 MB octal PSRAM (1.8 V) | ~440 µA typ |
| Light-sleep | +2 MB quad PSRAM (3.3 V) | ~280 µA typ |
| Deep-sleep | RTC memory + RTC peripherals on | 8 µA typ |
| Deep-sleep | RTC memory on, RTC peripherals off | 7 µA typ |
| Deep-sleep | ULP-FSM on | 170 µA typ |
| Deep-sleep | ULP-RISC‑V on | 190 µA typ |
| Power-off | CHIP_PU low | 1 µA typ |

**Note**: Wi‑Fi and Bluetooth must be explicitly disabled before entering light-sleep or deep-sleep; connections are not maintained through sleep modes.

---

## Power Delivery & Brownout Mitigation

The most prevalent cause of system instability in ESP32-S3 deployments is inadequate power delivery network (PDN) design, resulting in Brownout Detector (BOD) resets. The BOD monitors the 3.3 V rail and halts the CPU if voltage drops below ~2.43 V.

### Wi‑Fi TX Current Spikes

While the CPU draws 15–30 mA during normal execution, the RF power amplifier demands massive instantaneous current during Wi‑Fi transmission — **up to 500 mA within nanoseconds**. If the power supply, LDO, or PCB traces have high impedance, the resulting voltage droop triggers a BOD reset, creating an infinite boot loop as the device repeatedly attempts Wi‑Fi reconnection.

### PDN Mitigation Requirements

1. **LDO selection**: Avoid slow-response regulators (e.g., AMS1117). Use high-speed transient-response LDOs rated ≥600 mA: AP2112K-3.3, RT9080-33GJ5, or XC6220B331MR.
2. **Bulk decoupling**: 22–100 µF ceramic bulk capacitor directly at the module power entrance to supply instantaneous RF burst current.
3. **Pin-level decoupling**: 0.1 µF and 1 µF ceramic bypass capacitors as close as possible to VDD3P3_RTC, VDDA, and VDD_SPI pins.
4. **CHIP_PU RC delay**: 10 kΩ pull-up + 1 µF to ground. Never leave floating. Ensures the SoC is held in reset until the 3.3 V rail fully stabilizes.
5. **Cabling quality**: Poor USB-C cables (28 AWG) can introduce enough resistance to cause voltage drop during 500 mA spikes, triggering brownout regardless of on-board regulation.

---

## Analog Subsystem (ADC)

### Architecture

Two 12-bit SAR ADCs supporting up to 20 input channels. Internal reference voltage (Vref) ≈ 1.1 V. An attenuation network scales higher voltages before the SAR logic.

| Attenuation | Range (approx.) | Use Case |
|---|---|---|
| `ADC_ATTEN_DB_0` | 0–950 mV | Low-voltage analog sensors |
| `ADC_ATTEN_DB_2_5` | 0–1250 mV | Thermistors, precision current shunts |
| `ADC_ATTEN_DB_6` | 0–1750 mV | Mid-range signaling |
| `ADC_ATTEN_DB_12` | 0–2900 mV | High-voltage logic reading |

**Critical limitation**: At 12 dB attenuation, the linear range saturates at ~2900 mV. Voltages between 2.9 V and 3.3 V return the maximum ADC value (4095) — indistinguishable. For full-scale 0–3.3 V measurement (e.g., battery monitoring), use an external voltage divider to scale the signal into the linear region.

### Performance Specifications

Test conditions: external 100 nF capacitor on ADC input, DC signals, 25 °C, Wi‑Fi disabled.

- DNL: –4 to +4 LSB
- INL: –8 to +8 LSB
- Sampling rate: up to 100 kSPS
- Calibrated total error: ±5 mV (ATTEN0, 0–850 mV), ±6 mV (ATTEN1, 0–1100 mV), ±10 mV (ATTEN2, 0–1600 mV), ±50 mV (ATTEN3, 0–2900 mV)

For better DNL: oversample, average, or otherwise filter.

### Calibration

Factory calibration data is burned into eFuses on official modules. Firmware must use the curve-fitting calibration scheme via `adc_cali_create_scheme_curve_fitting()` to transform raw readings into accurate millivolt values. This reduces full-scale errors from multiple decivolts to approximately –30 to 0 mV.

### ADC2 & Wi‑Fi Hardware Conflict

ADC2 (GPIO11–GPIO20) is **physically shared with the Wi‑Fi RF transceiver PHY**. After `esp_wifi_start()`, `adc2_get_raw()` calls will fail with `ESP_ERR_TIMEOUT` or return invalid data. This is a hard silicon limitation. Systems requiring continuous analog sampling while transmitting Wi‑Fi **must** route all analog inputs to **ADC1** (GPIO1–GPIO10).

### ADC2 DMA Erratum [ADC-183]

The digital controller (DMA) of SAR ADC2 is **defective** on all current revisions (v0.0–v0.2). It may receive a false sampling-enable signal and enter an inoperative state. **ADC2 continuous mode is not supported on ESP32-S3.** Workaround: use the RTC controller to control SAR ADC2, or use ADC1 for continuous sampling.

### Design Guidance

The ESP32-S3 ADC is adequate for general sensing with calibration, filtering, and proper attenuation — but disappointing for precision metrology compared to an external ADC. Treat it as a **calibrated utility ADC**, not a precision instrumentation ADC.

---

## Capacitive Touch Sensor

The ESP32-S3 provides 14 capacitive touch GPIOs. These detect touch by charging/discharging the pin's intrinsic capacitance and measuring sawtooth-wave duration; human contact increases capacitance and lengthens the charge cycle.

### Limitations

- **EMI sensitivity**: Highly susceptible to electromagnetic interference and power supply noise.
- **Floating-pin false triggers**: GPIO34–39 lack internal pull-ups, causing false triggers from stray electrical fields.
- **Hardware freeze vulnerability**: If two touch pins are bridged or touched simultaneously with high-capacitance objects (large brass knobs, extensive copper foil), the internal FSM counter exceeds its maximum threshold (raw values spike to ~4,000,000), **locking up the touch hardware permanently**. `touchRead()` returns a stalled value indefinitely; a hard reset is required.

### Mitigation

Override default FSM timings with `touchSetCycles(1, 100)`. This forces a rapid 100 µs charge/discharge cycle that prevents counter overflow and silicon lock-up, at the cost of some extreme sensitivity.

### Erratum [TOUCH-100]

Raw analog data from the `TOUCH_SCAN_DONE_INT` interrupt is mathematically undefined and cannot be trusted for analog measurement or threshold triggering.

---

## USB Subsystem

The ESP32-S3 integrates **two distinct** USB peripherals — conflating them is a common PCB layout and firmware error.

### USB Serial/JTAG Controller (GPIO19/20)

A **fixed-function**, hardware-implemented endpoint for flashing firmware (via `esptool.py`), console logs, and OpenOCD JTAG debugging. Implemented entirely in silicon — no external USB-UART bridge (CP2102/CH340) required.

- **Cannot** be reprogrammed as HID, MSC, or other USB device classes.
- Physically mapped to GPIO19 (D–) and GPIO20 (D+).
- Relies on the APB clock — gated during sleep modes, killing the USB peripheral.
- On wake, the host PC does not automatically re-enumerate; the COM port disappears.

### USB OTG Controller

A **fully programmable** USB 2.0 full-speed interface supporting Device and Host modes. With TinyUSB, can act as USB Mass Storage, MIDI, HID keyboard/mouse, or host for external peripherals (barcode scanners, etc.). Uses the internal transceiver (shared with USB Serial/JTAG) or an external PHY.

### Sleep Mode USB Disconnection

When the ESP32-S3 enters light-sleep or deep-sleep, the APB clock is gated, killing the USB Serial/JTAG peripheral. Upon wake, the PC does not re-enumerate. Mitigation:

```cpp
Serial.flush();
Serial.end();
esp_light_sleep_start();
Serial.begin(115200);
```

Alternatively, manually pull USB D+ (GPIO20) low for a few milliseconds on wake to force a physical disconnect/reconnect cycle. For automatic sleep, use `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` in menuconfig.

### Debug Artifact: False RTC Memory Loss

`idf.py monitor` can reset the chip when the USB-serial device reappears after deep-sleep, with `USB_UART_CHIP_RESET` on reconnect. This makes persistent RTC memory data appear lost when the real culprit is the monitor reset. When validating deep-sleep retention, separate monitor behavior from silicon retention behavior.

---

## Peripherals & Protocol Limits

### UART

Three UART controllers. Asynchronous up to 5 Mbps. Support IrDA, RS‑232/RS‑485 style operation. Share 1024 × 8-bit RAM across TX/RX FIFOs of all three UARTs.

**Caveat**: UART DMA shares HCI hardware with Bluetooth. BT HCI and UART DMA **must not be used together**, even on different UART ports.

### I²C

Two controllers, master or slave. Datasheet advertises up to 800 kbit/s, but current ESP-IDF driver documentation caps master SCL frequency at **400 kHz** — treat 400 kHz as the conservative supported ceiling.

### LEDC (PWM)

Eight PWM channels. **Low-speed mode only** (no high-speed LEDC as on original ESP32). Clock sources: 80 MHz APB, ~20 MHz RC_FAST, 40 MHz XTAL. Frequency and duty resolution trade off against each other.

### RMT (Remote Control Transceiver)

Supports transmit, receive, carrier modulation/demodulation, and synchronous multi-channel transmission. On ESP32-S3: **4 dedicated TX channels + 4 dedicated RX channels** (structurally different from the legacy ESP32's 8 flexible channels). Eight channels share 384 × 32-bit internal RAM.

- **Erratum [RMT-176]**: Continuous-TX idle-state error on all current revisions. Verify idle levels in hardware if RMT drives protocols where idle state matters.
- **FastLED/WS2812B**: Maximum of **4 independent LED strips** natively. For DMA operations, `mem_block_symbols` must be strictly set to **1024** on S3; dynamic formulas based on LED count produce invalid arguments and `esp_cache_msync` errors.

### SD/MMC Controller

Supports 1-bit and 4-bit SD modes. Default pin mapping conflicts with Octal PSRAM (GPIO33–38). On 8 MB PSRAM modules, default SD card initialization **will crash the system** due to bus collision. Remedy: explicitly remap CLK, CMD, and DATA lines via `sdmmc_slot_config_t` to unencumbered GPIOs. Use external 10 kΩ pull-ups to VDD on all SD interface lines; internal weak pull-ups are insufficient.

### LCD_CAM Peripheral

Designed for parallel camera (DVP) and RGB/I8080 LCD interfaces via GDMA. Often co-opted for HUB75 LED matrix panels.

- **Erratum [LCD-239]**: When the LCD_CAM clock divider is set to 1 (pixel clock = system clock), the hardware loses falling-edge trigger capability and consecutive frames overlap. Set clock divider to **≥2** (cap LCD clock at 40 MHz on 80 MHz bus).
- **GDMA bandwidth**: Operates at APB clock (80 MHz), sharing SRAM/PSRAM access with CPU via round-robin arbitration. Maximum frame-buffer fetches from PSRAM can starve the CPU or miss LCD pixel-clock timings (visual tearing, dropped frames, data corruption).
- **HUB75 DMA**: At least one "dummy phase" must be programmed into the LCD_CAM state machine; without it, the DMA trigger fails to fire reliably.

### GDMA (General DMA)

Operates at APB clock (80 MHz). Shared SRAM/PSRAM access with CPU via round-robin arbitration. Peripheral DMA engines require DRAM-resident, word-aligned buffers. Use static `DMA_ATTR` buffers or `MALLOC_CAP_DMA` heap allocation. Do not use DMA buffers on stacks potentially in PSRAM.

---

## PCB Design, Layout & Bring-Up

### Stack-Up

Espressif recommends a **four-layer PCB** as default:
- **Top**: Signals and components
- **Layer 2**: Uninterrupted ground plane
- **Layer 3**: Power with careful isolation around RF and crystal
- **Bottom**: Minimal routing, ideally no components

Two-layer is possible but more defensive — easy isolation and return-path control are lost.

### Power Routing

- Wide 3.3 V traces, star-shaped branching after entry bulk capacitance.
- 10 µF at power entry.
- Individual 10 µF decoupling for RF-related VDD3P3 pins.
- Close 0.1 µF decouplers on digital power pins.
- 0.1 µF + 1 µF near VDD_SPI.
- At least nine ground vias from exposed pad into ground plane.
- Do not use excessively large capacitance on VDD_SPI.

### Crystal Layout

- Clean ground environment, no vias in crystal traces.
- No high-frequency routing underneath.
- Ground stitching around clock trace.
- Series components near chip side; matching capacitors beside the crystal (not against series parts).
- Minimum 2.0 mm gap to avoid interference.
- Keep-out area around crystal if top-layer ground permits.

### RF Layout

- 50 Ω impedance control.
- Chip-side CLC matching network close to chip.
- Short outer-layer routing without vias.
- Dense ground stitching.
- Physical separation from USB, UART, crystals, and switching activity.
- Ground reference quality matters as much as nominal trace width.

### USB Layout

- Reserve series resistor footprints (22–33 Ω) on D+ and D−, placed close to chip.
- Optional shunt capacitor footprints to ground.
- This is practical tuning for custom boards where trace impedance and connector choice aren't frozen.

### Flash/PSRAM Layout

- Validate flash/PSRAM models against Espressif support.
- Optional 0 Ω resistor footprints in series with SPI lines.
- Route SPI traces on inner layers with surrounding ground copper and vias.
- Match lengths for octal-SPI traces.
- If flash/PSRAM are physically far from the chip, decouple both VDD_SPI and the memories locally.

---

## Silicon Errata

All listed errata affect revisions v0.0, v0.1, and v0.2 unless noted. The most design-impacting errata:

### Critical (Design-Altering)

| Erratum | Description | Mitigation |
|---|---|---|
| **ADC-183** | SAR ADC2 digital controller (DMA) inoperative — false sampling-enable signal | Use ADC1 for continuous sampling, or ADC2 via RTC controller only |
| **ANALOG-160** | Permanent chip damage if `BIAS_SLEEP=0` and `PD_CUR=1` before sleep | Modern ESP-IDF removes this configuration; bare-metal code must avoid |
| **CACHE-126** | Cache write-back hit error during concurrent interrupt access → corrupted/duplicate data | ESP-IDF freezes opposite CPU and disables interrupts during write-backs |
| **RTC-126** | RTC register read errors after light-sleep wake-up if RTC peripherals powered down | Keep RTC peripherals powered during light-sleep (increases baseline current) |
| **RMT-176** | Continuous-TX idle-level error | Verify idle levels on target revision if idle state matters |
| **LCD-239** | LCD unreliable when clock divider = 1; overlapping frames | Set clock divider ≥ 2 |
| **TOUCH-100** | Undefined raw interrupt data from `TOUCH_SCAN_DONE_INT` | Do not use for analog measurement or threshold triggering |
| **USBOTG-4289** | USB-OTG download permanently disabled by eFuse on very early lots | Batch/revision awareness if depending on that boot path |

---

## Migration Notes & Community Experience

### From ESP32 / ESP32-S2

- **Do not carry over a "free pin" list blindly.** The S3 safe-pin budget is influenced by native USB, strap pins, JTAG/UART0 defaults, and flash/PSRAM topology.
- **No DAC**: The S3 has no DAC peripheral. Designs using ESP32 DAC features need external DACs.
- **I²S driver model** changed in modern ESP-IDF; per-channel APIs expose S3/C3 capabilities not present on ESP32/S2.
- **LEDC**: Low-speed mode only; no high-speed escape hatch.
- **RMT**: 4 TX + 4 RX dedicated channels (not 8 flexible).

### Application Architecture Migration Checklist

1. Decide whether to use USB Serial/JTAG as primary console or keep an external UART bridge.
2. Re-budget memory with PSRAM awareness and DMA-safe internal buffers.
3. Re-measure timing-sensitive code under DFS/light-sleep (interrupt latency up to ~40 µs).
4. Revisit analog assumptions — particularly ADC2 patterns and DAC features.

### Community-Verified Pitfalls

- **USB CDC & sleep**: Native USB CDC output stops after light-sleep; host may not re-enumerate. Mitigation: flush/end/begin sequence or manual D+ pulldown.
- **GPIO45 surprises**: Board-level strapping surprises with GPIO45 pull behavior are common. Burn `VDD_SPI_FORCE` eFuse in production.
- **Memory-related pins**: Engineers often read "not recommended" as "probably okay" for GPIO33–37, then discover bus crashes. Conservative rule: if a pin is memory-related in Espressif docs, use it only after exhausting safer alternatives.
- **Native USB enumeration timing**: Early serial output over native USB may be missed. Design boot logs with host-attach time in mind rather than assuming UART-like immediacy. Arduino-style environments may need `while(!Serial)` delays.
- **Dev-board current measurements**: Do not treat dev-board sleep-current measurements as module-level measurements without board-circuit isolation.

---

## Design Recommendations

### What to Avoid

- Using GPIO26–GPIO32 for application I/O, and GPIO33–GPIO37 on octal-memory variants, as ordinary spare pins.
- Loading GPIO0, GPIO3, GPIO45, or GPIO46 with circuits that override required reset-time strap levels.
- Treating GPIO19/20 as free GPIOs when flashing/debug depends on USB Serial/JTAG.
- Designing 1.8 V flash/PSRAM hardware without verifying VDD_SPI strap/eFuse configuration — **this can permanently damage memory devices.**
- Building continuous-sampling code around ADC2 DMA. The errata explicitly forbids it.
- Assuming 120 MHz flash/PSRAM DDR modes are production-safe. Espressif marks them experimental and temperature-sensitive.
- Measuring sleep current on a dev board and treating the result as module current.
- Repurposing GPIO19/20 or GPIO39–42 for outputs driving actuators without accounting for boot-time glitch states.

### Best Practices

- Reserve a genuinely safe GPIO subset early in schematic; document USB, straps, JTAG, UART0, and memory pins in the project pin budget.
- Use a four-layer PCB with uninterrupted ground plane, disciplined crystal placement, and RF/memory routing per Espressif stack-up guidance.
- Bias CHIP_PU properly with RC delay; pull GPIO0 up; keep reset/strap traces short and quiet.
- Prefer ADC1; add recommended local analog capacitor (100 nF); calibrate; average. Use an external ADC for precision work.
- Keep DMA buffers in internal DRAM with explicit DMA-capable allocation (`DMA_ATTR` or `MALLOC_CAP_DMA`).
- Decide early whether the primary console/debug path is UART, USB Serial/JTAG, or USB OTG firmware — this choice changes pin use, sleep behavior, and debug workflow.
- Treat flash/PSRAM configuration as a board-level contract between silicon variant, memory devices, VDD_SPI voltage, eFuses, and ESP-IDF configuration.
- Burn `VDD_SPI_FORCE` eFuse in production to eliminate the GPIO45 voltage-selection catastrophe hazard.
- For battery BLE products: include the external 32.768 kHz crystal — the internal RC oscillator does not meet BLE connection accuracy requirements.
- Verify `gpio_dump_io_configuration()` during bring-up to confirm pin reservations and GPIO-matrix routing.

---

## Key References

### Primary Sources (Espressif)

- **ESP32-S3 Series Datasheet** — Primary electrical, pin, feature, and current-consumption data.
- **ESP32-S3 Technical Reference Manual** — Detailed register-level peripheral and memory behavior.
- **ESP Hardware Design Guidelines for ESP32-S3** — Schematic checklist and PCB-layout rules.
- **ESP32-S3 Series SoC Errata** — Must-read before locking hardware/software assumptions.
- **ESP-IDF ESP32-S3 Target Documentation** — GPIO, sleep modes, external RAM, flash/PSRAM configuration, USB Serial/JTAG, LEDC, I²C, RMT, ULP, memory types.
- **esptool Documentation** — eFuse management, flash voltage configuration.

### Community & Framework References

- Espressif GitHub issue trackers (ESP-IDF, arduino-esp32)
- Espressif Forum (esp32.com)
- Zephyr RTOS ESP32-S3 documentation and Kconfig hierarchy
- FastLED, ESP32-HUB75-MatrixPanel-DMA repositories
- FluidNC ESP32-S3 Pin Reference
