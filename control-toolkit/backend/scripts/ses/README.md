# SES Actuator Bench Characterization Suite: Technical Guide

This document details the automated characterization, safety architecture, and Autoware Universe parameter extraction battery for the **SES (Steer-by-Wire) Actuator** using **CANalyst-II** on **Low-CAN (500 kbps)** under Windows.

---

## 1. Executive Summary & Objective

Prior bench testing of the SES subsystem relied on manual RC remote transmitter inputs (`rm-esp32`) and qualitative visual inspection. To transition smoothly into autonomous control under **Autoware Universe**, automated bench characterization suites were created to achieve three goals:

1. **Empirical Kinematic Profiling**: Measure usable steering travel, static position accuracy, directional hysteresis, slew limits, transport latency ($\tau$), 10–90% rise time, settling time, and closed-loop frequency bandwidth.
2. **Solving the "RMT_14 vs rm-esp32 Smoothness" Phenomenon**: Determine the mathematical and firmware root cause behind why legacy 5 Hz broadcasts appeared smooth while unfiltered 50 Hz broadcasts induced stick snap chatter.
3. **Hardware-Protected Automated Batteries**: Execute repeatable, non-destructive test sequences that safeguard bench power supplies and mechanical rack gears against brownout trips, plug-braking shock loads, and stator coil thermal runaway.

Four distinct versions were developed for progressive benchmarking:
* **V1** (`ses_characterization_bench.py`): Rapid 4-group baseline sweep (~51s runtime).
* **V2** (`ses_characterization_bench_v2.py`): Comprehensive 35-test physical battery with independent numerical velocity verification ($\Delta\theta/\Delta t$) (~80s runtime).
* **V3** (`ses_characterization_bench_v3.py`): Full 41-test suite incorporating frequency sweeps, alpha filtering, and deadband suppression (~89s runtime).
* **V4** (`ses_characterization_bench_v4.py`): 60-test combinatorial state-space, configuration discovery, timing sweeps, buffer evaluation, and fault injection battery (~190s runtime).

---

## 2. Low-CAN Protocol & Mathematical Encodings

The SES actuator interfaces over **Low-CAN at 500 kbps** with an 8-byte frame protocol:

### A. 0x169 Command Frame (VCU $\to$ SES @ 50 Hz)
* **Byte 0 Bit 1 (`VCU_SES_Control_Enable`)**: Autonomous mode enable. **Requires a strict $0 \to 1$ rising-edge handshake**. The harness always broadcasts 250 ms of disarmed neutral state (`0x00`) before asserting enable (`0x02`).
* **Bytes 1–2 (`VCU_SES_Tgt_StrAngle`)**: Target angle (Motorola Big-Endian, $0.1^\circ/\text{LSB}$).
  * Standard DBC Offset (-700°): $\text{Raw} = (\theta_{\text{deg}} + 700.0) \times 10$ (Neutral $0.0^\circ = 7000$ / `0x1B58`).
  * Legacy 3000 Offset: $\text{Raw} = (\theta_{\text{deg}} + 3000.0) \times 10$ (Neutral $0.0^\circ = 30000$ / `0x7530`).
  * *V4 automatically detects the active firmware offset at boot.*
* **Bytes 3–4 (`VCU_SES_Tgt_StrSpd`)**: Target slew rate (Motorola Big-Endian, $1^\circ/\text{s}/\text{LSB}$). Hardware clamp: $[125^\circ/\text{s}, 525^\circ/\text{s}]$.
* **Byte 5 (`Rolling_Counter_Enable` & `Checksum_Enable`)**: Life-signal validation bits (`0x03` = both enabled) + 4-bit alive counter in upper nibble.
* **Byte 6 (`VCU_Veh_Spd_Value`)**: Simulated vehicle speed ($1\text{ km/h}/\text{LSB}$). **Must be set $\ge 5\text{ km/h}$** (bench uses $10\text{ km/h}$ / `0x0A`). Setting $0\text{ km/h}$ when at $0.0^\circ$ center triggers low-power motor sleep.
* **Byte 7 (`Checksum`)**: Checksum byte using `xor8_ff_v1` profile (`XOR(Byte 0..6) ^ 0xFF`) with additive sum fallback.

### B. 0x201 Status Feedback Frame (SES $\to$ VCU @ 100 Hz)
* **Byte 0 Bit 0 (`SES_INF_Angle_Status`)**: Alignment bit. `1` = Aligned & calibrated; `0` = Mechanical zero reference unaligned.
* **Byte 0 Bits 1–2 (`SES_Control_Mode_Status`)**: `0` = Assist Mode, `1` = Autonomous Angle Control, `2` = Fault Mode, `3` = Manual Intervention (Driver Takeover).
* **Bytes 1–2 (`SES_StrAngle`)**: Actual measured rack angle ($0.1^\circ/\text{LSB}$, Big-Endian).
* **Bytes 3–4 (`SES_StrSpd`)**: Actuator actual velocity ($0.5^\circ/\text{s}/\text{LSB}$, Big-Endian).
* **Byte 5 (`SES_Driver_HandTorque`)**: Driver column torque sensor ($0.1\text{ Nm}/\text{LSB}$, $-12.1\text{ Nm}$ offset). Detects physical manual takeover.

### C. 0x202 Diagnostics & Error Frame (SES $\to$ VCU @ 10 Hz)
Monitors 25 discrete hardware error flags across Bytes 0–3, including L3 critical faults (`ECUTemp`, `DomainSC`, `StrMtrStall`, `MtrCurt`, redundant angle & torque sensor faults) and Byte 7 vehicle speed snapshot.

### D. 0x203 Version Frame (1 Hz) & 0x6FA Factory Telemetry Frame (100 Hz)
* **0x203**: Decodes firmware Software Version (`0.01 * B0`) and Hardware Version (`0.1 * B1`).
* **0x6FA**: Decodes real-time Motor Current (A), ECU Temperature (°C), and Bus Supply Voltage (V).

---

## 3. Comparison of the Test Scripts

All four scripts reside in `control-toolkit/backend/scripts/ses/`:

| Specification | V1 (`ses_characterization_bench.py`) | V2 (`ses_characterization_bench_v2.py`) | V3 (`ses_characterization_bench_v3.py`) | V4 (`ses_characterization_bench_v4.py`) |
| :--- | :--- | :--- | :--- | :--- |
| **Primary Scope** | Fast baseline bench sweep & Autoware extraction | Full 35-test physical battery with velocity derivative | Full 41-test suite + frequency, filtering & slew sweeps | Full 60-test combinatorial state-space, timing & fault battery |
| **Target Audience** | Quick validation / factory acceptance check | Detailed mechanical kinematic & control diagnostics | Control engineer tuning 50 Hz Autoware smoothing pipeline | Complete protocol qualification before vehicle Autoware integration |
| **Total Test Count** | 4 Sweep Groups | 35 Active Tests | 41 Active Tests | 60 Active Tests (11 Groups) |
| **Physical Runtime** | **~51 seconds** | **~80 seconds** | **~89 seconds** | **~18-190 seconds** (`discover` to `safe-core`) |
| **Auto-Detection** | Fixed (30000 offset) | Fixed (30000 offset) | Fixed (30000 offset) | **Dynamic (-700 vs 3000 offset, SW/HW ver, 0x6FA)** |
| **Timing Sweeps** | None | Fixed 50 Hz | 5, 20, 50, 100 Hz sweeps | **20, 50, 100 Hz + Jitter + Bursts + Gaps** |
| **Fault Injection** | None | None | Stubs | **Counter Freeze/Skip, Checksum Corrupt, Mode Debounce** |
| **Velocity Derivative** | Relies on reported 0x201 speed | Computes backward-difference $\Delta\theta/\Delta t$ | Computes $\Delta\theta/\Delta t$ + filter alpha tracking |
| **Frequency Sweep** | None (fixed 50 Hz) | None (fixed 50 Hz) | Sweeps 5 Hz, 20 Hz, 50 Hz, 100 Hz |
| **Filter Emulation** | None | None | Sweeps $\alpha = 0.08 - 0.25$ + 90°/s rate limiting |
| **Active Safety Mode** | `safe-core` (only) | `safe-core` (only) | `safe-core` (only) |
| **Interactive Mode** | Supported (`--mode interactive`) | Supported (`--mode interactive`) | Supported (`--mode interactive`) |

---

## 4. Test Suite Breakdown

### A. V1: Baseline Characterization (4 Test Groups)
1. **Group 1: Static Grid, Hysteresis & Repeatability Sweep**: Sweeps $[0^\circ, 10^\circ, 20^\circ, 10^\circ, 0^\circ, -10^\circ, -20^\circ, -10^\circ, 0^\circ]$ @ 200°/s with 1.5s holds. Measures absolute tracking error, mechanical backlash, and neutral return precision.
2. **Group 2: Slew Envelope Sweep**: Commands $0^\circ \to +20^\circ$ steps across $[150, 200, 250]^\circ/\text{s}$. Measures initial transport latency, 10–90% rise time, settling time, and overshoot.
3. **Group 3: Micro-Step & Deadband Analysis**: Steps $[\pm 0.5^\circ, \pm 1.0^\circ, \pm 1.5^\circ]$ to identify stick-slip threshold and steady-state deadband.
4. **Group 4: Sinusoidal Bandwidth Tracking**: Streams sinusoidal setpoints ($A = \pm 15^\circ$) at $0.2\text{ Hz}$, $0.5\text{ Hz}$, and $1.0\text{ Hz}$ with continuous feed-forward velocity setpoints to identify usable closed-loop tracking bandwidth.

### B. V2: Comprehensive 35-Test Physical Battery
* **Tests 01–05**: Protocol handshakes, alignment confirmation, positive direction, negative direction, and center return.
* **Tests 06–09**: Multi-point target accuracy, repeatability spread, left/right symmetry, and gear hysteresis.
* **Tests 10–18**: Commanded speed tracking, independent numerical derivative ($\Delta\theta/\Delta t$) cross-check, monotonic speed scaling, duration vs speed scaling, speed across varying angle spans, minimum controlled speed ($126^\circ/\text{s}$), safe maximum achievable speed ($250^\circ/\text{s}$ cap), speed linearity ($R^2 > 0.95$), and velocity repeatability.
* **Tests 19–21**: In-flight dynamic transitions:
  * *Test 19*: Mid-travel acceleration ($150^\circ/\text{s} \to 350^\circ/\text{s}$).
  * *Test 20*: Mid-travel deceleration ($350^\circ/\text{s} \to 150^\circ/\text{s}$).
  * *Test 21*: Mid-travel retargeting ($+10^\circ \to +22^\circ$).
* **Tests 23–32**: Transport latency ($\tau$), speed command response delay, 10–90% acceleration rise time, velocity ripple stability, approach deceleration profile, maximum overshoot, steady-state hunting check, settling time, 3-second hold drift stability, and zero-velocity target confirmation.
* **Tests 33–36**: Autonomous 50 Hz continuous wave tracking, 0.5 Hz sinusoidal tracking, column torque observation, and zero-speed vehicle sleep cutoff verification.

### C. V3: 41-Test Investigation Suite (RMT_14 vs rm-esp32)
Incorporates all baseline physical kinematics from V2, plus the targeted investigation into the legacy remote transmitter comparison:
* **Test 38 (Frequency Sweep)**: Evaluates control performance at 5 Hz, 20 Hz, 50 Hz, and 100 Hz. Quantifies how 5 Hz adds $>220\text{ ms}$ transport lag.
* **Test 39 (Minimum Slew Evaluation)**: Confirms that commanding the hardware minimum slew ($126^\circ/\text{s}$ / `0x007E`) creates mechanical low-pass damping.
* **Test 40 (Filter Alpha Sweep)**: Applies an exponential moving average low-pass filter:
  $$\theta_{\text{filt}}[k] = \alpha \cdot \theta_{\text{raw}}[k] + (1 - \alpha) \cdot \theta_{\text{filt}}[k-1]$$
  Evaluates $\alpha \in [0.08, 0.15, 0.25]$. Proves that $\alpha \approx 0.10$ delivers identical mechanical smoothness to RMT_14 while reducing latency from $220\text{ ms}$ to $55\text{ ms}$.
* **Test 41 (Software Rate Limiter)**: Emulates an in-flight velocity cap ($90^\circ/\text{s}$):
  $$\Delta\theta_{\max} = \text{rate\_limit} \times \Delta t$$
  Eliminates instantaneous stick snap chatter without stalling the motor.
* **Test 44 (Startup Settling Gate)**: Enforces a 2.5-second hold upon ignition before enabling autonomous control to eliminate startup jerks.
* **Test 45 (Trade-Off Optimization)**: Formulates the optimal Autoware Universe command profile.

---

## 5. Safety Architecture & Interlocks

To ensure that bench testing never damages equipment or components:

1. **Permanent Removal of `full` Stress Modes**:
   * All three scripts strictly accept `--mode safe-core` (or `interactive`).
   * Attempting to pass `--mode full` or `--mode full-*` is rejected immediately on the command line.
   * Internal calls to `run_all_tests()` automatically redirect to `run_safe_core()`.
2. **Power Supply Surge Protection ($250^\circ/\text{s}$ Safe Cap)**:
   * Actuator motions at $400^\circ/\text{s}$ pull $>30\text{ A}$ inrush current, causing brownout trips on standard 12V/15A bench power supplies.
   * All maximum speed tests are capped at $250^\circ/\text{s}$, keeping current draw safely $<12\text{ A}$.
3. **Mechanical Gear Protection (Test 22 Bypassed)**:
   * Test 22 (plug-braking direction reversal mid-flight) commands opposing torque while the rotor is spinning at full speed. This imposes massive shock loads on the internal worm gear.
   * Test 22 is permanently marked as `[BYPASSED FOR SAFETY]`. Direction reversals in other tests use decelerated transitions.
4. **Thermal Overheat Protection (Test 37 Bypassed)**:
   * Test 37 (stationary multi-cycle continuous endurance) overheats the motor stator coils because the stationary bench provides no ram-air cooling.
   * Test 37 is permanently marked as `[BYPASSED FOR SAFETY]`. Core test hold durations are limited to $1.0 - 1.5\text{ s}$.
5. **Acoustic Jitter & Security Protection (Tests 42 & 43 Bypassed)**:
   * Test 42 (synthetic jitter injection) is bypassed to prevent gear hunting.
   * Test 43 (security bypass / checksum disable) is bypassed to ensure 0x169 checksums are always valid.
6. **Following-Error Hardware Watchdog**:
   * A dedicated watchdog thread monitors tracking error every 10 ms.
   * If $|\theta_{\text{cmd}} - \theta_{\text{actual}}| \ge 4.0^\circ$ for $>300\text{ ms}$, the harness triggers an **instant emergency stop**, setting `control_enable = False` and cutting motor current.
7. **Safe Startup Disarm Gate**:
   * Commands transmit 250 ms of disarmed neutral state (`0x00`) before asserting enable (`0x02`), satisfying the controller's rising-edge requirement without jerking.

---

## 6. Directory Structure & File Output

To prevent root `logs/` directory clutter and avoid file overwrites:

* Each execution automatically creates a dedicated session folder inside `logs/ses_bench/`:
  ```
  e:\work\etrike\logs\ses_bench\session_YYYYMMDD_HHMMSS\
  ```
* Inside each session folder, two synchronized files are produced:
  1. **Telemetry Log (`.csv`)**: 50 Hz TX / 100 Hz RX synchronized stream (~150 rows/sec) logging timestamp, commanded angle, commanded slew, actual angle, actual speed, numerical derivative $\Delta\theta/\Delta t$, column torque, control mode, tracking error, and fault flags.
  2. **Characterization Report (`.md`)**: Performance metrics table, root-cause diagnostics, and Autoware Universe parameter configuration table.
* **Non-Destructive Crash/Interrupt Handling**: If an operator presses `Ctrl+C` or an unexpected exception occurs, the harness traps the event, terminates CAN transmission cleanly, and flushes all partial telemetry gathered up to that second into the session folder.

---

## 7. How to Run the Scripts

### A. Bench Execution (CANalyst-II on Low-CAN)
Ensure the CANalyst-II adapter is connected to the vehicle Low-CAN bus (Channel 1, 500 kbps, 120 $\Omega$ termination enabled):

```powershell
# Run V1 Baseline Battery (~51s)
python control-toolkit/backend/scripts/ses_characterization_bench.py

# Run V2 35-Test Battery (~80s)
python control-toolkit/backend/scripts/ses_characterization_bench_v2.py

# Run V3 Comprehensive Battery (~89s)
python control-toolkit/backend/scripts/ses_characterization_bench_v3.py
```

### B. Dry-Run Execution (Virtual Bus)
To verify software execution and reporting without physical hardware:

```powershell
python control-toolkit/backend/scripts/ses_characterization_bench_v3.py --interface virtual --channel 0
```

### C. Interactive Manual Tuning Mode
Allows real-time nudging and testing from the console:

```powershell
python control-toolkit/backend/scripts/ses_characterization_bench_v3.py --mode interactive
```
* **Interactive Commands**:
  * `<number>`: Set target angle in degrees (e.g. `15.0`, `-10.5`, `0`)
  * `s <number>`: Set target slew rate in deg/s (e.g. `s 200`)
  * `d` / `a`: Step $+5^\circ$ / $-5^\circ$
  * `0` / `c`: Return to Center ($0.0^\circ$)
  * `hz <val>`: Set transmission frequency (V3: e.g. `hz 50`)
  * `alpha <val>`: Set low-pass filter alpha (V3: e.g. `alpha 0.10`)
  * `e`: Immediate Emergency Stop (cuts motor control)
  * `q`: Clean exit and save logs

---

## 8. Recommended Autoware Universe Parameters

The characterization report generates production-ready parameters for Autoware Universe control nodes (`trajectory_follower`, `mpc_lateral_controller`):

| Autoware Node Parameter | Recommended Setting | Engineering Rationale |
| :--- | :--- | :--- |
| `max_steer_angle` | `0.436 rad` ($\pm 25.0^\circ$) | Confined within bench-verified mechanical envelope ($\pm 30.0^\circ$) |
| `max_steering_angle_rate` | `3.49 rad/s` ($200^\circ/\text{s}$) | 80% of safe-core cap ($250^\circ/\text{s}$) to avoid bench power supply brownouts |
| `steering_tau` / delay | `0.045 s` ($45\text{ ms}$) | Offsets CAN transport latency ($20\text{ ms}$) + mechanical inertia ($25\text{ ms}$) |
| `steering_lpf_cutoff_hz` | `1.20 Hz` | Filters trajectory frequencies beyond the physical closed-loop bandwidth |
| `goal_angle_tolerance` | `0.009 rad` ($\pm 0.50^\circ$) | Placed outside gear backlash deadband ($\pm 0.35^\circ$) to stop limit-cycle hunting |

---

## 9. Troubleshooting Common Bench Issues

1. **Actuator Does Not Engage (Stays in Manual Mode `0`)**:
   * *Cause*: Missing $0 \to 1$ rising-edge handshake on Byte 0 Bit 1.
   * *Fix*: Ensure the harness performs the 250 ms disarmed phase (`0x00`) before asserting `0x02`.
2. **Actuator Sleeps After a Few Seconds**:
   * *Cause*: Vehicle speed parameter in Byte 6 is set to $0\text{ km/h}$.
   * *Fix*: Ensure Byte 6 transmits $\ge 5\text{ km/h}$ (harness transmits $10\text{ km/h}$ / `0x0A`).
3. **Alignment Bit False (`0x201` Byte 0 Bit 0 = `0`)**:
   * *Cause*: Actuator has not performed mechanical center indexing upon power-up.
   * *Fix*: Move the steering column across center slowly or cycle 12V actuator logic power.
4. **Bench Power Supply Cuts Out During Rapid Steps**:
   * *Cause*: Bench supply current limit is set below 15A.
   * *Fix*: Set power supply overcurrent protection to $\ge 15\text{ A}$ (or 20A) and ensure `--mode safe-core` is active (slew $\le 250^\circ/\text{s}$).
5. **Following-Error Watchdog Trip (`Emergency Stop`)**:
   * *Cause*: Mechanical binding on tie-rods or target commanded beyond mechanical stops.
   * *Fix*: Inspect tie-rod clearance and verify `--max-angle` clamp is $\le 30.0^\circ$.
