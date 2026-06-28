# steer-by-wire EPS-C & SEB — Technical Reference

Extracted and cleaned from the original steer-by-wire product specification PDFs (`given-documentation.pdf`, `given-documentation-ocr.pdf`). The source PDFs are bilingual (Chinese/English) scanned documents; the OCR quality varies. This document distills the **technically actionable** information needed for firmware integration. Always verify byte layouts against the original PDFs before enabling actuator output.

---

## 1. EPS-C — Electric Power Steering Column

**Product:** Steer-by-wire intelligent steering system (SES)
**Version:** A/10 (2024-11-22)
**Category:** EPS-C

### 1.1 Technical Parameters

| Parameter | Value | Unit |
|-----------|-------|------|
| Assembly weight | 3.5–4 | kg |
| ECU weight | 0.4 | kg |
| Rated motor power | 360 | W |
| Rated motor speed | 1480 | rpm |
| Rated motor torque | 2.36 | Nm |
| Rated motor current | 52 | A |
| Rated working voltage | 12 | V |
| Max working current | 30 | A |
| ECU operating voltage | 9–15 | V |
| Debugging voltage | 13.5 | V |
| Spline teeth | 30/36 | z |
| Spline module | 0.467/0.47 | m |
| Spline pressure angle | 30.5/45 | ° |
| Max steering angle (wheel end) | ±700 | ° |
| Reduction ratio | 27 / 20.5 / 13.5 | — |
| Max steering torque | 55 / 41 / 27 | Nm |
| Steering accuracy | ±1 | ° |
| Max steering speed | 400 | °/s |
| Steering response time | <100 | ms |
| Max assist torque | (see datasheet) | Nm |

### 1.2 Communication

- **Baud rate:** 500K (default), 250K optional
- **Protocol:** CAN_SES (steer-by-wire proprietary steer-by-wire)
- **Terminal resistance:** Not populated by default on ECU

### 1.3 Operating Principle

The VCU transmits steering angle commands via CAN. The ECU calculates and drives the motor, which provides steering torque through a worm-gear reduction mechanism. An angle sensor monitors steering angle and feeds it back to the ECU via PWM. The ECU makes real-time adjustments and reports actual angle back to the VCU via CAN.

### 1.4 CAN Frames (SES Protocol)

#### VCU → EPS-C Command (ID: 0x169, cycle: 20 ms, DLC: 8)

| Signal | Description |
|--------|-------------|
| `VCU_SES_Alignment_Enable` | Alignment/zero-calibration enable |
| `VCU_SES_Control_Enable` | Steering control enable |
| `VCU_SES_Tgt_StrAngle` | Target steering angle |
| `VCU_SES_Tgt_StrAngleSpd` | Target steering angle speed |
| `VCU_SES_RollCnt_Enable` | Rolling counter enable |
| `VCU_SES_CheckSum_Enable` | Checksum enable |
| `VCU_SES_RollCnt` | Rolling counter (liveliness signal) |
| `VCU_Veh_Spd_Value` | Vehicle speed value |
| `VCU_SES_CheckSum` | Frame checksum |

> **⚠️ The exact byte layout, bit positions, scaling factors, and checksum algorithm must be verified against the project-specific protocol document before transmitting on hardware.** The `ksteer-by-wireCanOutputEnabled` flag in `config.h` defaults to `false` for this reason.

#### EPS-C → VCU Status (ID: 0x201, cycle: 10 ms, DLC: 8)

| Signal | Description |
|--------|-------------|
| `SES_INF_Angle_Status` | Actual angle feedback status |
| `SES_Control_Mode_Status` | Control mode status (0x0 = normal, 0x1 = fault level 1, 0x2 = fault level 2) |
| `SES_Error_Status` | Error/fault flags |
| `SES_StrAngle` | Actual steering angle |
| `SES_Tgt_StrAngleSpd` | Actual steering angle speed |
| `SES_SteeringWheel_Torq` | Steering wheel torque |
| `SES_RollCnt_Enable_Status` | Rolling counter valid |
| `SES_CheckSum_Enable_Status` | Checksum valid |
| `SES_RollCnt_Status` | Rolling counter echo |
| `SES_CheckSum_Status` | Checksum value |
| `SES_SW_Version` | Software version (separate frame, 1000 ms cycle) |
| `SES_HW_Version` | Hardware version (separate frame) |
| `SES_MtrCurt` | Motor current (10 ms cycle) |
| `SES_ECU_Temp` | ECU temperature |
| `SES_Anl_Volt` | Analog voltage |

### 1.5 EPS-C Fault Codes

| Fault | Level |
|-------|-------|
| ECU Under-voltage | L2 |
| ECU Over-voltage | L2 |
| CAN Communication Error | L1 |
| ECU Temperature | L3 |
| Domain drive SC (short circuit) | L3 |
| Domain drive V (voltage) | L3 |
| Domain drive T (temperature) | L3 |
| Temperature Sensor Error | L3 |
| Angle Sensor Channel O/C (open circuit) | L3 |
| Angle Sensor Channel AF | L3 |
| Angle Sensor S O/C | L3 |
| Angle Sensor S AF | L3 |
| Sensor Power Error | L3 |
| Alignment Error | L1 |
| Over-angle Error | L2 |
| Steering Stall Error | L3 |
| Motor Current Error | L3 |
| Sensor O/C Error | L3 |
| Torque Sensor T1 O/C | L3 |
| Torque Sensor T2 O/C | L3 |
| Torque Sensor T1 AF | L3 |
| Torque Sensor T2 AF | L3 |
| Sent Angle Error | L1 |
| Steering Ralling Error | L3 |
| EPPROM Error | L2 |

> Fault levels: L1 = highest severity, L4 = lowest severity

### 1.6 Installation Notes

- ECU must be zero-calibrated when matched to a new assembly, then re-powered
- After zero-calibration, the ECU + assembly pair is unique and not interchangeable
- ECU housing must be connected to chassis ground
- Low-voltage system shares common ground with the vehicle

---

## 2. SEB — Electronic Brake System

**Product:** Brake-by-wire controlled braking system (SEB)
**Version:** A/19 (2024-08-02)
**Category:** SEB

### 2.1 Technical Parameters

| Parameter | Value | Unit |
|-----------|-------|------|
| Max stroke | 27 | mm |
| Stroke accuracy | ±0.5 | mm |
| Stroke response time | ~50 | ms |
| Pressure-building response time | ~150 | ms |
| Max oil pressure | 5 | MPa |
| Brake fluid type | DOT4 | — |
| Operating voltage | 12 | V |

### 2.2 Operating Principle

The SEB is an electro-hydraulic brake system based on traditional hydraulic brake architecture. It consists of a drive motor, reduction mechanism, master cylinder, angle sensor, oil pressure sensor, and control unit. The VCU sends brake commands via CAN. The ECU drives the motor to build hydraulic pressure in the master cylinder, which actuates the calipers. Pressure and stroke sensors provide feedback.

### 2.3 CAN Frames (SEB Protocol)

#### VCU → SEB Command (ID: 0x7B0, cycle: 20 ms, DLC: 8)

| Signal | Description |
|--------|-------------|
| `VCU_SEB_Alignment_Enable` | Alignment enable |
| `VCU_SEB_Control_Enable` | Brake control enable |
| `VCU_SEB_Control_Mode` | Control mode (stroke or pressure) |
| `VCU_SEB_AutoBrake` | Automatic brake trigger |
| `VCU_SEB_Stroke_Value_Req` | Target stroke value |
| `VCU_SEB_Prs_Value_Req` | Target oil pressure value |
| `VCU_SEB_RollCnt_Enable` | Rolling counter enable |
| `VCU_SEB_CheckSum_Enable` | Checksum enable |
| `VCU_SEB_RollCnt` | Rolling counter |
| `VCU_SEB_CheckSum` | Frame checksum |

> **⚠️ Same warning as EPS-C:** verify byte layout and checksum algorithm before transmitting.

#### SEB → VCU Status (ID: 0x721, cycle: 10 ms, DLC: 8)

| Signal | Description |
|--------|-------------|
| `SEB_INF_Alignment_Status` | Alignment status feedback |
| `SEB_Control_Enable_Status` | Control enable status |
| `SEB_Control_Mode_Status` | Control mode feedback |
| `SEB_AutoBrake_Status` | Auto-brake status |
| `SEB_Error_Status` | Error flags (0x0 = normal, 0x1 = fault level 1, 0x2 = fault level 2) |
| `SEB_Stroke_Value` | Actual stroke |
| `SEB_Prs_Value` | Actual oil pressure |
| `SEB_AutoBrake_Value` | Auto-brake target value |
| `SEB_RollCnt_Enable_Status` | Rolling counter valid |
| `SEB_CheckSum_Enable_Status` | Checksum valid |
| `SEB_RollCnt_Status` | Rolling counter echo |
| `SEB_CheckSum_Status` | Checksum echo |
| `SEB_SW_Version` | Software version (1000 ms cycle) |
| `SEB_HW_Version` | Hardware version (1000 ms cycle) |

### 2.4 SEB Fault Codes

| Fault | Description |
|-------|-------------|
| `SEB_ECU_UnderVolt_Err` | ECU under-voltage |
| `SEB_ECU_OverVolt_Err` | ECU over-voltage |
| `SEB_CanCom_Err` | CAN communication timeout |
| `SEB_ECU_Temp_Err` | ECU over-temperature |
| `SEB_Domain_drive_SC_Err` | Motor driver short circuit |
| `SEB_Domain_drive_V_Err` | Motor driver voltage fault |
| `SEB_Domain_drive_T_Err` | Motor driver over-temperature |
| `SEB_AngleSensor_P_O/C_Err` | Angle sensor primary open circuit |
| `SEB_AngleSensor_P_AF_Err` | Angle sensor primary range fault |
| `SEB_AngleSensor_S_O/C_Err` | Angle sensor secondary open circuit |
| `SEB_AngleSensor_S_AF_Err` | Angle sensor secondary range fault |
| `SEB_NoPrsSensor_Err` | No pressure sensor detected |
| `SEB_Sensor_VCL_Err` | Sensor supply voltage fault |
| `SEB_Alignment_Err` | Alignment/zero-calibration error |
| `SEB_AngleOver_Err` | Angle exceeded limit |
| `SEB_Str_Stall_Err` | Stroke stall |
| `SEB_Mtr_O/C_Err` | Motor open circuit |
| `SEB_Oil_Err` | Hydraulic oil fault |
| `SEB_HotOil_Err` | Oil over-temperature |
| `SEB_Snsr_Value_Err` | Sensor value out of range |
| `SEB_Mtr_NoBrake_Err` | Motor fails to brake |
| `SEB_PrsSensor_Over_Err` | Pressure sensor over-range |
| `SEB_Low_Volt_Charging` | Low voltage charging fault |

### 2.5 Debug/Test Commands

The SEB supports test-mode commands for bleeding and calibration. These use CAN IDs `0x789` and `0x7B9` with a 9-byte payload (extended frame, DLC=8 with length byte preceding).

**Stroke mode test** (Byte0 = 0x02):
- Byte1–Byte2: Target stroke = `(physical_mm + 30) / 0.05`
- Max stroke: 27 mm
- Example 8mm stroke: `[02 58 00 00 00 00 00 00]` (with length=9 prefix)

**Oil pressure mode test** (Byte0 = 0x06):
- Byte3: Target oil pressure = `physical_MPa / 0.05`
- Max pressure: 5 MPa

**Cyclic venting** (for brake bleeding):
- Alternate between extend and retract stroke commands
- 2S interval between commands, 4S per cycle
- Repeat until no bubbles at caliper bleed screw

### 2.6 Installation Notes

- After installation, the oil circuit must be bled (non-air procedure using ECU stroke commands)
- Oil circuit is established when stroke command produces ≥2 MPa feedback pressure
- Use DOT4 brake fluid only
- Power-on pressure maintenance: before power-off, send 4 MPa pressure command; mechanical self-locking maintains ~4 MPa after power-off
- Avoid sustained >5 MPa commands — may cause motor overheating

---

## 3. DCDC Converter — CAN Protocol

**Document:** CAN BUS COMMUNICATION SPECIFICATION Version 1.2/1.3
**Baud rate:** 250 Kbps
**Frame format:** CAN 2.0B extended (29-bit ID), J1939-based

### 3.1 Node Addresses

| Node | Address (SA) |
|------|-------------|
| VCU | 0x27 |
| DCDC | 0x2B |

### 3.2 Message 1: VCU → DCDC (ID: 0x10262B27, 100 ms cycle)

| Byte | Signal | Scaling | Notes |
|------|--------|---------|-------|
| Byte0 | Control | Bit0–1: Working mode (00=disable, 01=enable). Bit2–7: Reserved | |
| Byte1 | Max Charging Voltage Low | 0.1 V/bit, offset 0 | e.g. Vset=140 → 14V |
| Byte2 | Max Charging Voltage High | 0.1 V/byte | |
| Byte3 | Max Charging Current Low | 0.1 A/bit, offset 0 | e.g. Aset=200 → 20A |
| Byte4 | Max Charging Current High | 0.1 A/byte | |
| Byte5 | Reserved | 0xFF | |
| Byte6 | Reserved | 0xFF | |
| Byte7 | Reset control | Bit0–1: Reset (00=no reset, 01=reset). Bit2–7: Reserved | |

- If VCU sends enable + zero voltage/current → DCDC outputs system default
- If no message received within 6s → DCDC enters communication error state and stops output

### 3.3 Message 2: DCDC → VCU (ID: 0x18F8622B, 100 ms cycle)

| Byte | Signal | Scaling | Notes |
|------|--------|---------|-------|
| Byte0 | System state | Table 1-1 (see §3.4) | |
| Byte1 | Temperature | 1°C/bit, offset 40°C | e.g. value 120 → 80°C |
| Byte2 | Output Voltage Low | 0.05 V/bit, offset 0 | |
| Byte3 | Output Voltage High | 0.05 V/byte | |
| Byte4 | Output Current Low | 0.05 A/bit, offset 0 | |
| Byte5 | Output Current High | 0.05 A/byte | |
| Byte6 | Error flags | Table 1-2 (see §3.5) | |
| Byte7 | Version info | Software version | |

### 3.4 DCDC System State (Byte0)

| Bits | Value | State |
|------|-------|-------|
| Bit0–2 | 000 | Stop |
| | 001 | Charging |
| | 010 | Charging complete |
| | 011 | Reserved |
| Bit3–4 | 00 | Fault Level 1 (highest) |
| | 01 | Fault Level 2 |
| | 10 | Fault Level 3 |
| | 11 | Fault Level 4 (lowest) |
| Bit5–7 | 000 | Ready |
| | 100 | Power Up |
| | 101 | Error |
| | 111 | Diagnostic/Calibration |

### 3.5 DCDC Fault Codes

| Code | Fault |
|------|-------|
| 11 | Output over-voltage |
| 12 | Output over-current |
| 101 | Input over-voltage |
| 102 | Input under-voltage |
| 103 | Over-temperature protection |
| 151 | CAN communication receive timeout |
| 152 | Output under-voltage |

---

## 4. Key Integration Notes

1. **Both EPS-C and SEB use proprietary CAN protocols with checksums and rolling counters.** Do not transmit actuator commands until the byte layout, scaling, and checksum algorithm are confirmed against the project-specific protocol document from steer-by-wire.

2. **EPS-C requires zero-calibration** when paired with a new assembly. This is a one-time procedure, likely triggered via the `Alignment_Enable` flag in the command frame.

3. **SEB requires brake bleeding** after installation. The ECU supports automated bleeding via stroke-mode test commands.

4. **Both ECUs have CAN termination disabled by default.** External 120Ω resistors must be installed at the physical ends of each CAN bus.

5. **EPS-C and SEB are on the private CAN bus** (TWAI1 on the ESP32-S3). They must never be exposed to the public Jetson CAN bus.

6. **VCU address 0x27** appears in the DCDC spec for J1939 addressing. The EPS-C and SEB protocols may use different addressing — verify with steer-by-wire.

7. **Fault handling:** Both ECUs report faults via status frames. Level-1 faults (L1) are most severe. The ESP32-S3 should monitor `Error_Status` and trigger ESTOP on critical faults.

---

*Source PDFs: `given-documentation.pdf` (58 pages, steer-by-wire EPS-C + DCDC CAN spec), `given-documentation-ocr.pdf` (58 pages, alternate scan with higher-quality text layer). Both preserved in the project root for byte-layout verification.*
