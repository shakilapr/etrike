# SEB (Brake-by-Wire) Actuator Technical Reference & Integration Guide

**Subsystem**: SEB (Braking Actuator / Brake-by-Wire / Electro-Hydraulic Brake)  
**Target Controllers**: `sys-esp32`, `rt-esp32`, `rm-esp32-t12d`, `control-toolkit`, Hardware Simulator  
**Bus Architecture**: Low-CAN (500 kbps, 11-bit Standard Identifiers)  
**Document Status**: Official Engineering Reference  

---

## 1. System Overview & Actuator Role

The **SEB (Brake-by-Wire)** unit is an electro-hydraulic brake actuator that drives hydraulic pressure into the brake calipers via CAN commands. It integrates a DC drive motor, mechanical reduction gear, master hydraulic cylinder, pushrod linear travel sensor, hydraulic pressure transducer, and an embedded microcontroller running closed-loop position and pressure PID loops.

```
                      +------------------------------------------------+
                      |               Low-CAN (500 kbps)              |
                      +-------+----------------+---------------+-------+
                              |                ^               ^
           0x7B9 VCU_SEB_REQ  |                | 0x721 STATUS  | 0x731 ERR_INFO
           (50 Hz / 20 ms)    |                | (100 Hz / 10ms| (10 Hz / 100ms)
                              v                |               |
                      +------------------------------------------------+
                      |          SEB (Brake-by-Wire Actuator)          |
                      |                                                |
                      |  [ Motor Driver ] ───> [ Master Cylinder ]     |
                      |         │                     │                |
                      |         v                     v                |
                      |  [ Pushrod Stroke ]    [ Fluid Pressure ]      |
                      |    (0.1 mm/LSB)          (0.05 MPa/LSB)        |
                      +------------------------------------------------+
```

### Key Operating Parameters
| Parameter | Value | Notes |
|:---|:---|:---|
| **CAN Bus** | Low-CAN | Dedicated actuator bus |
| **Bitrate** | 500 kbps (default) | 250 kbps, 500 kbps, 1000 kbps selectable |
| **Command ID** | `0x7B9` (`VCU_SEB_REQ`) | Transmitted by active brake controller (`sys-esp32` or `rt-esp32`) |
| **Command Rate** | 20 ms (50 Hz) | Actuator enters communication fault if silent > 20 ms |
| **Status ID** | `0x721` (`SEB_STATUS`) | Actuator transmits continuously (100 Hz / 10 ms) |
| **Fault Info ID** | `0x731` (`SEB_ErrInfo`)| Actuator transmits fault bitmap (10 Hz / 100 ms) |
| **Version ID** | `0x741` (`SEB_Version`)| Software & hardware version info (1 Hz / 1000 ms) |
| **Test ID** | `0x6FB` (`SEB_Test`)   | Extended diagnostic / bleeding interface (default disabled) |
| **Byte Ordering** | **Motorola Big-Endian**| MSB first for multi-byte values (Stroke, Pressure, Angle) |
| **Stroke Range** | 0 to 27.0 mm | Resolution: **0.1 mm/LSB** (Protocol Note 11) |
| **Pressure Range**| 0 to 5.0 MPa | Resolution: **0.05 MPa/LSB** (Protocol Note 12) |

---

## 2. CAN Wire Protocol Specifications

### 2.1 Command Frame: `0x7B9 VCU_SEB_REQ` (50 Hz / 20 ms Period)

DLC: 8 Bytes. Transmitted periodically by the VCU/SYS controller.

| Byte | Bits | Field Name | Type | Scale | Offset | Raw Range | Physical / Description |
|:---:|:---:|:---|:---:|:---:|:---:|:---:|:---|
| **0** | `0` | `VCU_SEB_Alignment_Enable` | bool | 1 | 0 | `0..1` | `1` = Zero-point alignment (resets current position to 0 mm) |
| **0** | `1` | `VCU_SEB_Control_Enable` | bool | 1 | 0 | `0..1` | `1` = Enable active closed-loop brake control |
| **0** | `2` | `VCU_SEB_Control_Mode` | bool | 1 | 0 | `0..1` | **`0` = Stroke Mode**, **`1` = Pressure Mode** |
| **0** | `3` | `VCU_SEB_AutoBrake` | bool | 1 | 0 | `0..1` | `1` = Enable 5-second CAN-timeout 4 MPa autonomous brake |
| **0** | `4..7`| Reserved | — | — | — | `0` | Must be `0` |
| **1** | `0..7`| `VCU_SEB_Stroke_Value_Req [15:8]`| uint16| 0.1 | 0 | `0..270` | Target Stroke MSB (**Big-Endian**, $0.1\text{ mm/count}$) |
| **2** | `0..7`| `VCU_SEB_Stroke_Value_Req [7:0]` | uint16| 0.1 | 0 | `0..270` | Target Stroke LSB (**Big-Endian**, $0.1\text{ mm/count}$) |
| **3** | `0..7`| `VCU_SEB_Pre_Value_Req` | uint8 | 0.05 | 0 | `0..100` | Target Pressure ($0.05\text{ MPa/count}$, Max $5.0\text{ MPa} = 100$) |
| **4** | `0..7`| Reserved | — | — | — | `0` | Must be `0x00` |
| **5** | `0..7`| Reserved | — | — | — | `0` | Must be `0x00` |
| **6** | `0` | `VCU_SEB_RollCnt_Enable` | bool | 1 | 0 | `1` | **Must be set to `1`** |
| **6** | `1` | `VCU_SEB_CheckSum_Enable`| bool | 1 | 0 | `1` | **Must be set to `1`** |
| **6** | `2..3`| Reserved | — | — | — | `0` | Must be `0` |
| **6** | `4..7`| `VCU_SEB_RollCnt` | uint4 | 1 | 0 | `0..15` | Rolling counter: increments 1 each frame ($0 \to 15 \to 0$) |
| **7** | `0..7`| `VCU_SEB_CheckSum` | uint8 | 1 | 0 | `0..255` | Frame Checksum (see §3.2 for algorithm details) |

#### Stroke Value Calculation:
$$\text{RawStroke} = \text{round}\left(\frac{\text{Stroke}_{\text{mm}}}{0.1}\right) = \text{Stroke}_{\text{mm}} \times 10$$

- **Brake Released ($0.0\text{ mm}$)**: $\text{Raw} = 0$ (`0x0000`)
- **Light Braking ($5.0\text{ mm}$)**: $\text{Raw} = 50$ (`0x0032`)
- **Medium Service Brake ($15.0\text{ mm}$)**: $\text{Raw} = 150$ (`0x0096`)
- **Full Emergency Brake ($27.0\text{ mm}$)**: $\text{Raw} = 270$ (`0x010E`)

*(Note on Legacy Offsets: Certain earlier tooling configured stroke as `(mm + 30) / 0.05` where 0 mm was 600 raw. In the current SEB specification, the resolution is explicitly $0.1\text{ mm}$, where $0\text{ mm}$ aligns to 0).*

#### Hydraulic Pressure Value Calculation:
$$\text{RawPressure} = \text{round}\left(\frac{\text{Pressure}_{\text{MPa}}}{0.05}\right) = \text{Pressure}_{\text{MPa}} \times 20$$

- **$0.0\text{ MPa}$**: $\text{Raw} = 0$
- **$1.0\text{ MPa}$**: $\text{Raw} = 20$ (`0x14`)
- **$2.0\text{ MPa}$**: $\text{Raw} = 40$ (`0x28`)
- **$4.0\text{ MPa}$ (Holding / AutoBrake)**: $\text{Raw} = 80$ (`0x50`)
- **$5.0\text{ MPa}$ (Max Pressure)**: $\text{Raw} = 100$ (`0x64`)

---

### 2.2 Status Frame: `0x721 SEB_STATUS` (100 Hz / 10 ms Period)

DLC: 8 Bytes. Transmitted periodically by the SEB internal microcontroller.

| Byte | Bits | Field Name | Type | Scale | Offset | Description |
|:---:|:---:|:---|:---:|:---:|:---:|:---|
| **0** | `0` | `SEB_INF_Alignment_Status` | bool | 1 | 0 | Zero-Point Alignment: `0` = Calibrating/Searching, `1` = Aligned |
| **0** | `1` | `SEB_Control_Enable_Status` | bool | 1 | 0 | Control Status: `0` = Disabled, `1` = Active Closed-Loop |
| **0** | `2..3`| `SEB_Control_Mode_Status` | uint2 | 1 | 0 | Active Mode: `0` = Stroke Mode, `1` = Pressure Mode |
| **0** | `4` | `SEB_AutoBrake_Status` | bool | 1 | 0 | Autonomous Brake Status: `1` = AutoBrake armed |
| **0** | `5` | Reserved | — | — | — | Reserved |
| **0** | `6..7`| `SEB_Error_Status` | uint2 | 1 | 0 | Fault Level: `0` = OK, `1` = L1 Minor, `2` = L2 Fault, `3` = L3 Critical |
| **1** | `0..7`| `SEB_Stroke_Value [15:8]` | uint16| 0.1 | 0 | Measured Stroke MSB (**Big-Endian**, $0.1\text{ mm/count}$) |
| **2** | `0..7`| `SEB_Stroke_Value [7:0]` | uint16| 0.1 | 0 | Measured Stroke LSB (**Big-Endian**, $0.1\text{ mm/count}$) |
| **3** | `0..7`| `SEB_Pressure_Value` | uint8 | 0.05 | 0 | Measured Fluid Pressure ($0.05\text{ MPa/count}$) |
| **4** | `0..7`| `SEB_Angle_Value [15:8]` | int16 | 0.5 | 0 | Motor Rotor Angle MSB (**Big-Endian**, $0.5^\circ\text{/count}$) |
| **5** | `0..7`| `SEB_Angle_Value [7:0]` | int16 | 0.5 | 0 | Motor Rotor Angle LSB (**Big-Endian**, $0.5^\circ\text{/count}$) |
| **6** | `0` | `SEB_RollCnt_Enable_Status` | bool | 1 | 0 | Echoed rolling counter enable (`1`) |
| **6** | `1` | `SEB_CheckSum_Enable_Status`| bool | 1 | 0 | Echoed checksum enable (`1`) |
| **6** | `2..3`| Reserved | — | — | — | Reserved |
| **6** | `4..7`| `SEB_RollCnt_Status` | uint4 | 1 | 0 | Echoed rolling counter (0..15) |
| **7** | `0..7`| `SEB_CheckSum_Status` | uint8 | 1 | 0 | Echoed checksum |

---

### 2.3 Diagnostic Fault Frame: `0x731 SEB_ErrInfo` (10 Hz / 100 ms Period)

DLC: 8 Bytes. Emitted by the actuator to provide a comprehensive bit-level diagnostic fault map:

| Byte | Bit | Signal Name | Description | Severity |
|:---:|:---:|:---|:---|:---:|
| **0** | `0` | `SEB_ECUUnderVolt_Err` | Supply voltage $< 9.0\text{ V}$ | L2 |
| **0** | `1` | `SEB_ECUOverVolt_Err` | Supply voltage $> 16.0\text{ V}$ | L2 |
| **0** | `2` | `SEB_CanCom_Err` | VCU command frame `0x7B9` timeout ($> 20\text{ ms}$) | L1 |
| **0** | `3` | `SEB_ECUTemp_Err` | ECU PCB temperature over limit | L3 |
| **0** | `4` | `SEB_Domaindrive_SC_Err`| Motor H-bridge / gate driver short circuit | L3 |
| **0** | `5` | `SEB_Domaindrive_V_Err` | Motor gate driver supply voltage fault | L3 |
| **0** | `6` | `SEB_Domaindrive_T_Err` | Motor driver stage over-temperature | L3 |
| **0** | `7` | `SEB_AngleSensor_PDC_Err`| Primary rotor angle sensor open circuit | L3 |
| **1** | `0` | `SEB_AngleSensor_PAF_Err`| Primary angle sensor out-of-range | L3 |
| **1** | `1` | `SEB_AngleSensor_SOC_Err`| Secondary angle sensor open circuit | L3 |
| **1** | `2` | `SEB_AngleSensor_SAF_Err`| Secondary angle sensor out-of-range | L3 |
| **1** | `3` | `SEB_NOPreSensor_Err` | Pressure sensor disconnected or unreadable | L2 |
| **1** | `4` | Reserved | Reserved | — |
| **1** | `5` | `SEB_SensorsCL_Err` | Sensor 5V power supply rail fault | L3 |
| **1** | `6` | `SEB_Alignment_Err` | Pushrod zero-point calibration error | L2 |
| **1** | `7` | `SEB_AngleOver_Err` | Motor rotor angular travel exceeded physical limits | L3 |
| **2** | `0` | `SEB_SMtr_Stall_Err` | Pushrod drive motor mechanical stall | L3 |
| **2** | `1` | `SEB_MtrO_C_Err` | Motor winding open circuit | L3 |
| **2** | `2` | `SEB_Oil_Err` | Hydraulic circuit leak / pressure-build failure | L2 |
| **2** | `3` | `SEB_InitOil_Err` | Initial fluid filling / bleeding incomplete | L2 |
| **2** | `4` | `SEB_SamValue_Err` | Internal ADC sampling error | L3 |
| **2** | `5` | `SEB_Mtr_Noload_Err` | Motor spinning without pushrod resistance (linkage disconnected)| L3 |
| **2** | `6..7`| Reserved | Reserved | — |
| **3** | `0` | `SEB_PreSensorOver_Err` | Fluid pressure exceeded max rating ($> 5.5\text{ MPa}$) | L3 |
| **3** | `1` | `SEB_LowVoltCharging_Err`| Auxiliary low-voltage circuit charging error | L2 |
| **3..7**| `—` | Reserved | Bytes 4 through 7 reserved | — |

---

## 3. Operational Rules & Actuator Behavior

### 3.1 Operating Modes: Stroke Mode vs. Pressure Mode

* **Stroke Mode (`Control_Mode = 0`)**:
  * Recommended for standard electronic braking and E-Stop holding.
  * The internal PID drives the motor until the linear pushrod position matches `VCU_SEB_Stroke_Value_Req`.
  * Stroke response time: $\sim 50\text{ ms}$.

* **Pressure Mode (`Control_Mode = 1`)**:
  * Used for precise hydraulic brake torque control.
  * The internal PID adjusts pushrod displacement until fluid pressure measured by the built-in transducer matches `VCU_SEB_Pre_Value_Req`.
  * Pressure-build response time: $\sim 150\text{ ms}$.

### 3.2 Checksum Algorithm Evaluation
Across the actuator family, two checksum algorithms exist:
1. **8-bit Additive Sum** (Verified on physical SES hardware):
   $$\text{CheckSum} = \left(\sum_{i=0}^6 \text{Byte}_i\right) \pmod{256}$$
2. **XOR8 with Inversion** (`XOR8_FF`):
   $$\text{CheckSum} = \left(\bigoplus_{i=0}^6 \text{Byte}_i\right) \oplus \text{0xFF}$$

> [!TIP]
> When testing on a new bench SEB unit:
> Transmit a benign released command (`Stroke = 0`, `Pressure = 0`). If the actuator rejects frames or logs checksum errors in `0x721`, toggle between Additive Sum and XOR8. Because SES strictly requires Additive Sum, SEB hardware running modern actuator firmware typically shares the same additive sum logic.

### 3.3 Note 10: Autonomous Braking (`AutoBrake`)
* When `VCU_SEB_AutoBrake` (Byte 0, Bit 3) is set to `1`, the SEB arms an internal 5-second deadman watchdog.
* If the VCU stops transmitting `0x7B9` commands for $> 5.0\text{ seconds}$, the SEB independently spins the motor to generate **$4.0\text{ MPa}$ hydraulic holding pressure**.
* **Safety Implication**: If the vehicle controller crashes or the Low-CAN bus breaks, the actuator locks the wheels automatically.
* Note: This mode resets to disabled on every power cycle and must be explicitly re-enabled after boot if required.

### 3.4 Note 14: Mechanical Pressure-Holding Interlock
* When holding a sustained brake command, if hydraulic pressure is $\le 4.0\text{ MPa}$ and motor current $> 1.0\text{ A}$ continuously for $> 1.0\text{ s}$, the actuator engages an internal mechanical lock (self-locking lead screw / detent) to maintain hydraulic pressure without overheating the motor.
* When a new target command arrives, the actuator automatically disengages the mechanical lock and tracks the new setpoint.

---

## 4. Multi-ECU Arbitration on Low-CAN (RT vs. SYS)

The vehicle architecture features two potential brake command producers: `sys-esp32` (Primary Safety Controller) and `rt-esp32` (Autonomous Trajectory Controller).

```
                     +---------------------------+
                     |  Manual Brake Lever (SYS) │
                     +-------------┬-------------+
                                   │
                                   v
+----------------+          +─────────────+          +----------------+
|  rt-esp32      |          |  sys-esp32  |          |  SEB Actuator  |
|  (AUTO Mode)   |          |  (Safety)   |          |  (0x7B9 RX)    |
+───────┬────────+          +──────┬──────+          +───────▲────────+
        │                          │                         │
        │ 0x205 RT_BRAKE_CMD       │ 0x7B9 VCU_SEB_REQ       │
        └─────────────────────────>│ (Sole Active Producer)──┘
                                   │
                                   │ (If SYS disappears >250ms)
                                   └─────────[ RT Emergency Takeover ]
```

1. **Sole Producer Rule**: `sys-esp32` is the sole normal transmitter of `0x7B9` on Low-CAN.
2. **Arbitration**: In AUTO mode, `rt-esp32` transmits desired brake torque over `0x205 RT_BRAKE_CMD`. `sys-esp32` arbitrates between manual lever inputs, E-Stop states, and RT requests, outputting the consolidated `0x7B9` command.
3. **Emergency Fallback**: `rt-esp32` monitors `0x7B9`. Only if `sys-esp32` completely halts transmission for $> 250\text{ ms}$ does `rt-esp32` break silence and assert emergency E-Stop braking on `0x7B9`.

---

## 5. Reference Implementation (C++ Codec & Driver)

```cpp
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace etrike::seb {

constexpr uint32_t kCommandId = 0x7B9;
constexpr uint32_t kStatusId  = 0x721;
constexpr uint32_t kErrInfoId = 0x731;

enum class Mode : uint8_t {
    Stroke   = 0,
    Pressure = 1
};

struct Command {
    bool    align_enable{false};
    bool    control_enable{true};
    Mode    mode{Mode::Stroke};
    bool    auto_brake{false};
    float   stroke_mm{0.0f};       // 0.0 to 27.0 mm
    float   pressure_mpa{0.0f};    // 0.0 to 5.0 MPa
    uint8_t rolling_counter{0};
};

inline uint8_t calc_sum8(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum += data[i];
    return static_cast<uint8_t>(sum & 0xFF);
}

inline void encode_command(const Command& cmd, uint8_t out[8]) {
    out[0] = (cmd.align_enable ? 0x01 : 0x00) |
             (cmd.control_enable ? 0x02 : 0x00) |
             ((static_cast<uint8_t>(cmd.mode) & 0x01) << 2) |
             (cmd.auto_brake ? 0x08 : 0x00);

    // Stroke: 0.1 mm/count, Motorola Big-Endian (Bytes 1-2)
    float clamped_stroke = std::clamp(cmd.stroke_mm, 0.0f, 27.0f);
    uint16_t raw_stroke = static_cast<uint16_t>(std::round(clamped_stroke * 10.0f));
    out[1] = static_cast<uint8_t>((raw_stroke >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>(raw_stroke & 0xFF);

    // Pressure: 0.05 MPa/count (Byte 3)
    float clamped_prs = std::clamp(cmd.pressure_mpa, 0.0f, 5.0f);
    out[3] = static_cast<uint8_t>(std::round(clamped_prs / 0.05f));

    // Reserved Bytes 4 & 5
    out[4] = 0x00;
    out[5] = 0x00;

    // Security Byte 6: RollCntEn (b0=1), ChecksumEn (b1=1), RollCnt (b4..7)
    out[6] = 0x03 | (static_cast<uint8_t>(cmd.rolling_counter & 0x0F) << 4);

    // Checksum Byte 7
    out[7] = calc_sum8(out, 7);
}

} // namespace etrike::seb
```

---

## 6. Diagnostics & Troubleshooting Matrix

| Symptom | Root Cause | Fix / Verification |
|:---|:---|:---|
| **Actuator does not move; `0x721` shows `Error_Status = 1` and `0x731` Byte 0 Bit 2 is high** | CAN Communication Timeout (`SEB_CanCom_Err`) | Command frame `0x7B9` must be transmitted continuously at 50 Hz ($20\text{ ms} \pm 2\text{ ms}$). Gaps $> 20\text{ ms}$ trigger fault. |
| **Actuator completely ignores frames; rolling counter does not echo in `0x721`** | Checksum algorithm mismatch or missing enable flags | Ensure Byte 6 has `0x03` in low nibble (`RollCnt_Enable` and `CheckSum_Enable`). If Additive Sum fails, try `XOR8_FF`. |
| **Actuator engages full brake after 5 seconds of silence** | Note 10 Autonomous Brake triggered | `AutoBrake` (Byte 0, Bit 3) was enabled. Maintain continuous 50 Hz transmission or clear Bit 3 if bench testing without continuous VCU frames. |
| **Brake travel does not reach mechanical maximum** | Stroke resolution mismatch | Verify scaling factor is $0.1\text{ mm}$ per count ($27\text{ mm} = 270\text{ raw}$). Legacy $0.05\text{ mm}$ configurations will only stroke half-distance ($13.5\text{ mm}$). |
| **`0x731` reports `SEB_Alignment_Err` (Byte 1, Bit 6)** | Zero-point drift | Send `Alignment_Enable = 1` with brake fully released and hydraulic circuit bled to calibrate zero datum. |
