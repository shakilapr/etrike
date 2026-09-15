# Hardware Bench Test Suite Guide (RT-ESP32 & SYS-ESP32)

Automated verification guide for testing the combined **RT-ESP32** and **SYS-ESP32** controllers on the hardware bench with absent third-party actuators (MTR, SEB, and SES) simulated over CAN.

---

## 1. System Topology Under Test

```
                          HIGH CAN BUS (500 kbit/s)
  ┌────────────────────────────────────────────────────────────────────────┐
  │                                                                        │
  │  ┌──────────────────┐                               ┌──────────────┐   │
  │  │   RT ESP32-S3    │                               │ CANalyst-II  │   │
  │  │  MCP2515 (SPI)   │◄─────────────────────────────►│ Ch0 (High)   │   │
  │  │                  │   Host 0x300, 0x301, 0x302,   │ 120Ω Term ON │   │
  │  │                  │   0x303, 0x7FC, 0x111, 0x112  └──────────────┘   │
  │  └────────┬─────────┘                                      ▲           │
  │           │                                                │           │
  └───────────┼────────────────────────────────────────────────┼───────────┘
              │  RT Gateway (Forwarding High ↔ Low)            │
  ┌───────────┼────────────────────────────────────────────────┼───────────┐
  │           │           LOW CAN BUS (500 kbit/s)             │           │
  │  ┌────────┴─────────┐         ┌──────────────────┐         │           │
  │  │   RT ESP32-S3    │         │   SYS ESP32-S3   │         │           │
  │  │   TWAI (Built-in)│         │   TWAI (Built-in)│         │           │
  │  └──────────────────┘         └──────────────────┘         │           │
  │           ▲                            ▲                   │           │
  │           └──────────────┬─────────────┘                   │           │
  │                          │                                 ▼           │
  │                          │                          ┌──────────────┐   │
  │                          └─────────────────────────►│ Ch1 (Low)    │   │
  │                             Actuator Simulation     │ 120Ω Term OFF│   │
  │                             0x201, 0x721, 0x120/0x206└──────────────┘   │
  └────────────────────────────────────────────────────────────────────────┘
                                                               ▲
                                                               │ Python REST
                                                    ┌────────────────────┐
                                                    │ Control Toolkit    │
                                                    │ Backend (:8001)    │
                                                    │ Hardware Test Suite│
                                                    └────────────────────┘
```

---

## 2. Hardware Wiring Checklist

| Node | Interface | Pins | Bus | Termination | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **RT ESP32-S3** | MCP2515 SPI | SCK=15, MOSI=16, MISO=17, CS=18, INT=47 | **High CAN** | None (CANalyst provides) | 3.3V transceiver |
| **RT ESP32-S3** | TWAI (built-in) | TX=GPIO5, RX=GPIO4 | **Low CAN** | **120 Ω ON** | WCMCU-230 module |
| **SYS ESP32-S3** | TWAI (built-in) | TX=GPIO5, RX=GPIO4 | **Low CAN** | **120 Ω ON** | WCMCU-230 module |
| **SYS ESP32-S3** | Digital I/O | GPIO 1 -> GND | — | — | Active-LOW E-Stop NC switch (Must be grounded) |
| **CANalyst-II** | Channel 0 | High CAN Backbone | **High CAN** | **120 Ω ON** | Pluggable green terminal |
| **CANalyst-II** | Channel 1 | Low CAN Backbone | **Low CAN** | **120 Ω OFF** | Bus total impedance = 60 Ω |

### Pre-Power Multimeter Checks (Power OFF)
- **Low Bus CAN_H ↔ CAN_L**: $\sim 60\ \Omega$ (RT $120\ \Omega \parallel$ SYS $120\ \Omega$)
- **High Bus CAN_H ↔ CAN_L**: $\sim 120\ \Omega$ (CANalyst-II Ch0)
- **High Bus ↔ Low Bus**: $\infty\ \Omega$ (Completely galvanically isolated buses)
- **CAN_H / CAN_L ↔ GND**: $> 1\ \text{k}\Omega$ (No ground shorts)

---

## 3. Absent Actuator Peer Simulation

Because MTR (motor), SEB (brake), and SES (steering) are not yet physically wired, the test suite automatically synthesizes their CAN feedback at 50 Hz to keep RT and SYS state machines healthy:

| Absent Unit | CAN ID | Message Name | Simulated Value | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **SES Steering** | `0x201` | `SES_STATUS` | `angle=0`, `angle_aligned=1` | Keeps RT steering state machine in `STEER_ACTIVE` |
| **SEB Brake** | `0x721` | `SEB_STATUS` | `stroke=0`, `status=1 (OK)` | Satisfies SYS brake task staleness check |
| **MTR Motor** | `0x120` | `SYS_THROTTLE_STS`| `speed_mmps = target` | SYS EGAS L2 speed consistency & Host telemetry |
| **MTR Feedback**| `0x206` | `MTR_MOTOR_FBK` | `speed = target`, `gear = D` | Closed-loop setpoint echo |
| **Host** | `0x7FC` | `HOST_HEARTBEAT` | `alive_ctr++`, `health_flags=1` | Keeps RT host heartbeat monitor active |

---

## 4. Test Catalog Overview

### Suite 1: Baseline & Physical Discovery
- `test_1_1_rt_high_heartbeat`: RT `0x7FD` @ 2 Hz on High Bus with monotonic counter.
- `test_1_2_rt_high_state_rpt`: RT `0x210` @ 10 Hz on High Bus (`mode=MANUAL`, `estop_reason=0`).
- `test_1_3_sys_low_heartbeat`: SYS `0x7FE` @ 10 Hz on Low Bus with all task health bits OK.
- `test_1_4_sys_low_safety_sts`: SYS `0x011` @ 5 Hz on Low Bus with valid AUTOSAR CRC8.
- `test_1_5_rt_low_drive_cmd_idle`: RT `0x204` @ 100 Hz on Low Bus (`speed=0`, `gear=N`).
- `test_1_6_rt_gateway_forwarding`: Low-to-High gateway forwarding of `0x011` and `0x600`.

### Suite 2: Static Command & Kinematics Propagation
- `test_2_1_host_heartbeat_tracking`: RT tracks Host `0x7FC` and asserts health flag in `0x7FD`.
- `test_2_2_host_drive_forward_kinematics`: `0x300` ($1000\text{ mm/s}$, D) $\rightarrow$ RT `0x204` ($1000\text{ mm/s}$, D) on Low Bus.
- `test_2_3_host_drive_reverse_kinematics`: `0x300` ($-500\text{ mm/s}$, R) $\rightarrow$ RT `0x204` ($-500\text{ mm/s}$, R) on Low Bus.
- `test_2_4_host_brake_request_arbitration`: `0x301` ($4000\text{ kPa}$) $\rightarrow$ RT `0x205` ($4000\text{ kPa}$) on Low Bus.
- `test_2_5_host_light_gateway_sys`: `0x302` High $\rightarrow$ Low $\rightarrow$ SYS updates headlights/turn signals in `0x011`.
- `test_2_6_hmi_power_gateway_sys`: `0x112` High $\rightarrow$ Low $\rightarrow$ SYS emits `0x113` (`power_state=ON`).

### Suite 3: Actuator Peer Simulation Subsystem
- `test_3_1_simulated_ses_steering_feedback`: Synthetic `0x201` prevents RT steering `STEER_FAULT`.
- `test_3_2_simulated_seb_brake_feedback`: Synthetic `0x721` prevents SYS brake fault warnings.
- `test_3_3_simulated_mtr_feedback`: Synthetic `0x120` & `0x206` forwarded to High Bus for Host logging.

### Suite 4: Safety & Emergency Stop Reactions
- `test_4_1_high_bus_estop`: `0x001` on High Bus $\rightarrow$ RT halts motor, forwards to Low $\rightarrow$ SYS latches ESTOP.
- `test_4_2_low_bus_estop`: `0x001` on Low Bus $\rightarrow$ SYS latches ESTOP, opens relay $\rightarrow$ RT enters ESTOP.
- `test_4_3_host_watchdog_timeout`: Loss of `0x300` stream triggers $100\text{ ms}$ RT watchdog $\rightarrow$ speed drops to 0.
- `test_4_4_sys_drive_cmd_staleness`: Halting `0x204` zeroes SYS speed setpoint within $200\text{ ms}$.
- `test_4_5_estop_recovery_and_rearm`: `0x114` reset request $\rightarrow$ SYS `0x115` ACCEPTED $\rightarrow$ nominal clear.

### Suite 5: Complex Dynamic Multi-Step Maneuvers
- `test_5_1_startup_and_drive_ready_lifecycle`: Cold boot $\rightarrow$ Power ON $\rightarrow$ Peers ON $\rightarrow$ AUTO transition $\rightarrow$ RT motion authority enabled.
- `test_5_2_dynamic_acceleration_ramp`: Smooth acceleration $0 \rightarrow 500 \rightarrow 1200 \rightarrow 2200\text{ mm/s}$ at 100 Hz.
- `test_5_3_dynamic_slalom_cornering`: Left yaw ($\pm 400\text{ mrad/s}$) with turn lights $\rightarrow$ counter-steer $\rightarrow$ high-speed dynamic steering clamp verification at $3000\text{ mm/s}$.
- `test_5_4_blended_trail_braking`: Cruising at speed $\rightarrow$ partial braking ($3000\text{ kPa}$) $\rightarrow$ motor throttle cut $\rightarrow$ brake light on $\rightarrow$ release & recover.
- `test_5_5_dynamic_obstacle_deceleration`: Cruising at $1500\text{ mm/s}$ $\rightarrow$ obstacle distance decays $\rightarrow$ automatic deceleration to safe halt.
- `test_5_6_driver_takeover_interlock`: Driver manual override in AUTO $\rightarrow$ SYS drops mode to MANUAL $\rightarrow$ RT instantly revokes autonomous authority.
- `test_5_7_gear_shift_directional_interlock`: Shifting to Reverse while cruising forward is blocked by RT until vehicle is stationary.

---

## 5. Running the Test Suite

### Preflight Check
Verify CANalyst-II detection and drivers:
```powershell
python control-toolkit/backend/scripts/canalyst_preflight.py
```

### Full Hardware Bench Execution
Run all 27 tests against the physical RT and SYS setup:
```powershell
python control-toolkit/backend/scripts/hardware_bench_suite.py --profile bench_test --report bench_report.json
```

### Running Specific Suites or Tests
```powershell
# Run only dynamic driving maneuvers:
python control-toolkit/backend/scripts/hardware_bench_suite.py --suite dynamic -v

# Run only safety & ESTOP tests:
python control-toolkit/backend/scripts/hardware_bench_suite.py --suite safety

# Run a single test by name substring:
python control-toolkit/backend/scripts/hardware_bench_suite.py --test slalom
```

---

## 6. Real-Time Live UI Monitoring

Open **http://127.0.0.1:5173/** in a web browser:
1. **Pipeline View**: Observe the real-time cascade of `0x300` (High) $\rightarrow$ RT $\rightarrow$ `0x204` (Low).
2. **Signal Charts**: Watch live waveform traces for speed ramping, steering angle changes, and brake pressure.
3. **Safety Status Tile**: Monitor ESTOP state, heartbeat health flags, and power relay status.
