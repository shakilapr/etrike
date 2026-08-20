# SYS ESP32-S3 — Sensing Plan

**Date:** 2026-08-20  
**ECU:** SYS ESP32-S3 (Safety & Body Controller)  
**Status:** Approved Sensing Architecture  

---

## 1. Overview

This document defines the sensing plan for the SYS ESP32-S3 ECU based on the voltage divider and I2C ADC architecture:
1. **Gear Shifter & Speed Sensing:** Direct GPIO inputs via resistive voltage dividers.
2. **Throttle Sensing:** 16-bit analog-to-digital conversion via external ADS1115 module over I2C.

---

## 2. Pin Assignments & Circuitry

### 2.1 Gear Shifter & Speed Inputs (Direct GPIO with Voltage Dividers)

Signals from the gear shifter and speed sensor pass through resistive voltage dividers ($R_1 / R_2$) to scale voltages safely down to 3.3V logic levels for the ESP32-S3.

| ESP32 GPIO | Signal | Subsystem / Function | Input Circuit / Details | Notes |
|:----------:|:-------|:---------------------|:------------------------|:------|
| **GPIO 7** | **Parking Gear** | Gear Shifter | Voltage Divider ($R_1 / R_2$) | Replaces previous headlight switch |
| **GPIO 10** | **Speed** | Speed Sensor | Voltage Divider ($R_1 / R_2$) | Pulse / speed signal; replaces headlight relay driver |
| **GPIO 12** | **Drive Gear** | Gear Shifter | Voltage Divider ($R_1 / R_2$) | Previously bench-only D gear |
| **GPIO 13** | **Reverse Gear** | Gear Shifter | Voltage Divider ($R_1 / R_2$) | Previously bench-only S gear; now Reverse |

```
Signal Input ───[ R1 ]───┬───[ GPIO Pin ]
                         │
                       [ R2 ]
                         │
                        GND
```

*Note on Bench vs New Plan:*
- In the original bench-only setup, only **Drive (GPIO 12)**, **Sport (GPIO 13)**, and **Reverse (GPIO 14)** were defined. There was **no pin assigned for Parking**.
- In this new plan, **Parking** is added on **GPIO 7**, **Drive** remains on **GPIO 12**, **Reverse** moves to **GPIO 13**, **Speed** is added on **GPIO 10**, and **GPIO 14** is freed.

---

### 2.2 Throttle Sensing (ADS1115 I2C ADC)

Analog throttle input is digitized via an external **ADS1115 16-bit ADC** communicating with the ESP32-S3 over I2C.

| ADS1115 Pin | Connected To | Description |
|:------------|:-------------|:------------|
| **VDD** | ESP32 **3.3 V** (3V3) | Module Power Supply |
| **GND** | ESP32 **GND** | Ground Reference |
| **SCL** | ESP32 **GPIO 15** | I2C Clock Line |
| **SDA** | ESP32 **GPIO 16** | I2C Data Line |
| **ADDR** | **GND** | Sets I2C Address to **`0x48`** |
| **ALRT** | *Unconnected / NC* | Alert / Ready interrupt pin |
| **A0** | **Throttle Signal Wire** | Analog input from throttle grip |
| **A1–A3** | *Unused / Reserved* | Spare ADC channels |

#### Throttle Input Divider Circuit (at A0):
```
5V Throttle Signal ───[ R1 = 10 kΩ ]───┬───[ ADS1115 A0 ]
                                       │
                                 [ R2 = 20 kΩ / 18 kΩ ]
                                       │
                                      GND
```
- **$R_1$:** 10 kΩ
- **$R_2$:** 20 kΩ (or 18 kΩ)
- Voltage at A0: $V_{A0} = 5\text{V} \times \frac{20\text{k}}{10\text{k} + 20\text{k}} \approx 3.33\text{V}$ (or $\approx 3.21\text{V}$ with 18 kΩ), matching the ADS1115 3.3V operating range.

---

## 3. Comparison with Previous Bench Setup

| Function | Old Bench Setup | New Sensing Plan | Change / Impact |
|:---------|:----------------|:-----------------|:----------------|
| **Parking Gear** | *None (no pin assigned)* | **GPIO 7** | Added with voltage divider ($R_1/R_2$) |
| **Speed Sensor** | *None on SYS* | **GPIO 10** | Added with voltage divider ($R_1/R_2$) |
| **Drive Gear** | GPIO 12 (Bench-only) | **GPIO 12** | Maintained Drive function on GPIO 12 |
| **Reverse Gear** | GPIO 14 (Bench-only) | **GPIO 13** | Moved from 14 to 13 (replaces old S gear) |
| **Sport Gear** | GPIO 13 (Bench-only) | *Removed / Replaced* | Repurposed for Reverse Gear |
| **I2C Clock** | GPIO 15 (MCP4725 SCL) | **GPIO 15** (ADS1115 SCL) | Reused I2C SCL for ADS1115 ADC |
| **I2C Data** | GPIO 16 (MCP4725 SDA) | **GPIO 16** (ADS1115 SDA) | Reused I2C SDA for ADS1115 ADC |
| **Headlight Switch** | GPIO 7 | *Displaced* | Reassign to free GPIO (e.g. GPIO 14 / 22) |
| **Headlight Relay** | GPIO 10 | *Displaced* | Reassign to free GPIO (e.g. GPIO 14 / 22) |

---

## 4. Firmware Updates Required (`sys-esp32`)

1. **`config.h` Updates:**
   - Define `kGearParkGpio = 7`
   - Define `kSpeedSensorGpio = 10`
   - Define `kGearDriveGpio = 12`
   - Define `kGearReverseGpio = 13`
   - Define `kI2cSclGpio = 15`, `kI2cSdaGpio = 16`
   - Define `kAds1115I2cAddr = 0x48`
   - Reassign or comment out `kSwitchHeadlight` (GPIO 7) and `kLightHead` (GPIO 10).

2. **Driver Implementations:**
   - ADS1115 I2C driver integration for reading Throttle on channel A0.
   - Speed sensing pulse measurement on GPIO 10.
   - Digital reading / debouncing for Park (7), Drive (12), Reverse (13).
