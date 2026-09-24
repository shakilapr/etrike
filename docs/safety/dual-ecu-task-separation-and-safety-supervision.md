# Dual-ECU Task Separation & Safety Supervision Architecture

> **Subsystems:** `sys-esp32` (Master Safety Authority & Body Controller) and `rt-esp32` (Autonomous Motion Master & Gateway)  
> **Target Platform:** Dual ESP32-S3 Architecture over Dual CAN Buses (High CAN & Low CAN)  
> **Status:** Active Architectural Reference  

---

## 1. Executive Summary & Design Principles

The vehicle safety architecture employs a **dual-controller safety architecture** with clear separation of duties and defense-in-depth redundancy:

```
                            ┌─────────────────────────────────────────┐
                            │            Host (High-Level)            │
                            │      Perception, Planning, Routing      │
                            └────────────────────┬────────────────────┘
                                                 │ High CAN (500 kbit/s)
                                                 ▼
                            ┌─────────────────────────────────────────┐
                            │                rt-esp32                 │
                            │      Autonomous Motion Master &         │
                            │           Dual-Bus Gateway              │
                            └────────────────────┬────────────────────┘
                                                 │ Low CAN (500 kbit/s)
         ┌───────────────────────┬───────────────┴───────────────┬───────────────────────┐
         ▼                       ▼                               ▼                       ▼
  ┌──────────────┐        ┌──────────────┐                ┌──────────────┐        ┌──────────────┐
  │  sys-esp32   │        │  mtr-stm32   │                │  SES Unit    │        │  SEB Unit    │
  │ Master Safety│        │ Traction /   │                │ Steer-by-Wire│        │ Brake-by-Wire│
  │   Authority  │        │ Power Cont.  │                │   Actuator   │        │ Smart Actuat.│
  └──────────────┘        └──────────────┘                └──────────────┘        └──────────────┘
```

### Core Separation Principles

1. **Safety Authority vs. Motion Execution**:
   * **SYS (`sys-esp32`)** is the **sole master of vehicle state and power**. It owns the master operating mode state machine (`MANUAL`, `AUTO`, `ESTOP`), the high-voltage (72V) contactor power interlock, and the hardware safety inputs (physical mushroom ESTOP button, handlebar brake switch).
   * **RT (`rt-esp32`)** is the **motion client and kinematics resolver**. It owns trajectory generation, dynamic stability envelopes (dynamic speed clamps and slew rate limiters), and steer-by-wire command generation. RT never commands power contactors and is completely silent on actuator control while in `MANUAL`.

2. **Defense-in-Depth (Dual-Layer Actuator Supervision)**:
   * When an actuator experiences communication loss or failure, **both** controllers detect and respond according to their sphere of authority:
     * **SYS** enforces system-level safety: cuts power authority (`0x113`), inhibits mode entry, or triggers vehicle-wide ESTOP.
     * **RT** enforces control-loop integrity: immediately zeroes high-frequency motion setpoints (within 10 ms at 100 Hz), disables trajectory tracking, and requests deceleration.

3. **Fail-Safe Authority Bootstrapping**:
   * RT powers on with zero drive authority (`g_no_sys_authority = true`). It cannot command motion until SYS has proven full health by continuously publishing valid safety and mode frames.

---

## 2. Controller Responsibility Matrix

| Subsystem Domain | Master Authority | Client / Supervisor | Separation Boundary & Authority Invariant |
| :--- | :--- | :--- | :--- |
| **Operating Mode** | **SYS** (`0x110 SYS_MODE_CMD`) | **RT** (Follower) | SYS decides mode (`MANUAL`, `AUTO`, `ESTOP`). RT executes motion only when SYS mode is `AUTO`. |
| **High-Voltage 72V Power** | **SYS** (`0x113 SYS_PWR_CMD`) | **None** (MTR interlock) | SYS is the sole emitter of contactor enable commands. RT has zero power/relay control. |
| **Hardware ESTOP Button** | **SYS** (GPIO 1 direct read) | **RT** (via CAN `0x001` / `0x011`) | Fail-safe NC loop wired directly to SYS high-priority safety task (Core 0, Priority 5). |
| **Traction Control (MTR)** | **RT** (`0x204 RT_DRIVE_CMD`) | **SYS** (EGAS L2 Supervisor) | RT generates 100 Hz speed commands in `AUTO`. SYS supervises command-path agreement against `0x206`. |
| **Braking Control (SEB)** | **SYS** (`0x7B9 VCU_SEB_REQ`) | **RT** (Emergency Fallback) | SYS is the sole normal producer of `0x7B9`. RT only takes over `0x7B9` if SYS vanishes from Low CAN. |
| **Steering Control (SES)** | **RT** (`0x169 VCU_SES_REQ`) | **SYS** (Passive Observer) | RT owns 50 Hz steering setpoints, kinematics, dynamic rollover clamps, and angle slew limits. |
| **Host Gateway (High CAN)** | **RT** (Dual-Bus Gateway) | **SYS** (Receives via Low CAN) | RT terminates High CAN (SPI MCP2515) and translates / filters commands to Low CAN (TWAI). |

---

## 3. Subsystem Supervision & Fault Handling Breakdown

### 3.1 Traction & Propulsion Motor Subsystem (MTR)

Propulsion relies on three coordinated streams: `0x204 RT_DRIVE_CMD` (RT to MTR), `0x206 MTR_MOTOR_FBK` (MTR to bus), and `0x113 SYS_PWR_CMD` (SYS to MTR).

```
 ┌─────────────┐                      0x204 RT_DRIVE_CMD (100 Hz)                      ┌─────────────┐
 │  rt-esp32   ├──────────────────────────────────────────────────────────────────────►│  mtr-stm32   │
 └──────┬──────┘                                                                       └──────┬──────┘
        │                              0x206 MTR_MOTOR_FBK (20 Hz)                            │
        │◄───────────────────────────────────┬────────────────────────────────────────────────┤
        │                                    │                                                │
        │                                    ▼                                                │
        │                             ┌─────────────┐                                         │
        │                             │  sys-esp32  │◄────────────────────────────────────────┘
        │                             └──────┬──────┘      0x206 Fault Flags (ESTOP ACK)
        │                                    │
        │                                    │ 0x113 SYS_PWR_CMD (10 Hz)
        │                                    ▼
        └──────────────────────────────► (Power Contactor Cut)
```

#### Dual Supervision of MTR Feedback (`0x206` Staleness):
* **Trigger**: No `0x206` received for $> 200\,\text{ms}$ (`kMtrFbkTimeoutMs` on RT, `kMtrFbkStaleMs` on SYS).
* **Action by SYS (`sys-esp32`)**:
  * Sets transient inhibit bit `kInhibitMtrFbkLoss` in `g_inhibit_reasons`.
  * Revokes high-voltage power authority: `0x113 SYS_PWR_CMD` sets power enable to `OFF` (opens 72V contactors).
  * Forces vehicle mode transition away from `AUTO` to `MANUAL` / standstill.
  * Zeroes internal speed setpoint memory (`g_setpoint_speed_mmps = 0`).
  * **Recovery**: Clears automatically after 3 consecutive fresh `0x206` observations at 20 Hz cadence.
* **Action by RT (`rt-esp32`)**:
  * Immediately cuts drive command generation: `r.zero_setpoints = true` drops `0x204` speed setpoints to 0 within 10 ms.
  * Sets `g_mtr_health.mtr_unavailable = true`: blocks any planner or path tracker from issuing drive commands.
  * Dynamic brake escalation: if the vehicle was actively commanded to drive within the last 500 ms, RT asserts max brake pressure (`r.brake_kpa = shared::kMaxBrakeKpa`) to safely halt momentum.
  * Emits diagnostic code `DiagId::RtMtrFbkTimeout`.
  * **Recovery**: Confirmed after 3 consecutive fresh `0x206` frames.

#### Additional MTR Safety Checks:
* **Host Speed & Runaway Authority**:
  * Autonomy stack on the Host (Jetson) tracks physical vehicle odometry, IMU, and perception to detect speed deviations and runaways.
  * Artificial low-level command-path comparisons (`0x204` vs `0x206` echo) are eliminated from SYS to prevent nuisance ESTOP lockouts caused by control lag.
* **MTR Autonomous Failsafe & Supervision**:
  * Upon entering ESTOP, SYS broadcasts `0x001 SAFETY_ESTOP`, asserts `estop_active = 1` across `0x011`, and revokes power authority (`0x113 = OFF`).
  * MTR autonomously clamps throttle DAC to 0.0V and opens direction relays immediately upon receiving `0x001` or `0x011`. If CAN comms are severed, MTR's internal 150 ms `0x204` watchdog and 500 ms CAN deadman ensure immediate zero-torque de-energization.

---

### 3.2 Braking Subsystem (SEB)

Braking requires continuous availability and definitive ownership. The architecture employs a **single-normal-producer model with fail-safe emergency takeover**.

```
  [Rider Brake Lever] (GPIO 2) ──┐
                                 ▼
                          ┌─────────────┐
  [RT Autonomous Brake] ─►│  sys-esp32  ├────────► 0x7B9 VCU_SEB_REQ (50 Hz) ────────► [SEB Smart Actuator]
     (0x205 50 Hz)        └──────┬──────┘                                                    │
                                 │                                                           ▼
                                 │ 0x7B9 missing > 100 ms?                                 0x721
                                 ▼                                                      (Feedback)
                          ┌─────────────┐                                                    │
                          │  rt-esp32   ├────────► 0x7B9 EMERGENCY TAKEOVER ─────────────────┤
                          └─────────────┘                                                    ▼
                                                                                   [Both RT & SYS Check]
```

#### Dual Supervision of Brake Actuator:
* **SYS Level (Normal Operation & Stroke Enforcement)**:
  * **Arbitration**: SYS arbitrates between physical handlebar lever (highest priority), RT autonomous brake request (`0x205`), and ESTOP full stroke (27.0 mm).
  * **Stroke Excursion Supervision**: Compares commanded cylinder stroke against actual cylinder stroke reported in `0x721`.
  * **Transient Inhibit (`kInhibitBrakeFollowing`)**: Stroke error $> 3.0\,\text{mm}$ persisting $> 100\,\text{ms}$ sets transient inhibit; drops 72V traction power and prevents AUTO drive. Recovers after 3 clean frames.
  * **Latched Safety Fault (`kLatchedBrakeFollowing`)**: Stroke error persisting $\ge 500\,\text{ms}$ escalates to a latched safety fault, cutting traction power and clamping mode to MANUAL while preserving steering (SES) control.
  * **SEB Critical Diagnostic (`kLatchedSebL3`)**: If `0x721` or `0x731` reports Level 3 hardware error, SYS immediately latches a brake safety fault, cutting 72V traction power and clamping mode to MANUAL while keeping steer-by-wire (SES) alive for controlled stopping.
* **RT Level (Emergency Fallback Writer)**:
  * RT normally emits brake intent via `0x205 RT_BRAKE_CMD` (50 Hz) and **never** writes `0x7B9`.
  * RT continuously monitors Low CAN specifically for the presence of SYS's `0x7B9` stream (independent of heartbeat).
  * If SYS's `0x7B9` disappears while armed (`EMERGENCY_FALLBACK`):
    * RT asserts `0x001 SAFETY_ESTOP`.
    * RT takes over sole transmission of `0x7B9 VCU_SEB_REQ`, sending emergency max-brake stroke directly to the actuator.
    * Latched handback: RT releases `0x7B9` only after SYS heartbeat is healthy and a minimum number of valid SYS `0x7B9` frames are confirmed on the bus.

---

### 3.3 Steer-by-Wire Subsystem (SES)

RT is the sole master of the steering actuator (`0x169 VCU_SES_REQ`). SYS acts as an observer and mode provider.

* **Software Mechanical Hard-Stops**: RT clamps all commanded steering angles to $\pm 40.0^\circ$ on every 100 Hz cycle to protect physical steering stops.
* **Dynamic Rollover Clamp**: RT dynamically limits maximum steering angle inversely with vehicle speed:
  $$\text{Limit}(\text{speed}) = 40.0^\circ - (\text{speed} - 2.0\,\text{km/h}) \times \frac{35.0^\circ}{23.0\,\text{km/h}}$$
  Clamped between $5.0^\circ$ (at $\ge 25\,\text{km/h}$) and $40.0^\circ$ (at $\le 2\,\text{km/h}$).
* **Dynamic Slew-Rate Limiter**: Limits steering angular rate between $125^\circ/\text{s}$ (low speed) and $400^\circ/\text{s}$ (high speed).
* **Following Error Supervision**:
  * Compares commanded angle (`0x169`) against feedback reported in `0x201`.
  * Dynamic threshold: $\max(2.0^\circ, 0.25 \times \text{dynamic\_limit})$.
  * If error persists $> 300\,\text{ms}$, RT trips local ESTOP (`kEstopReasonFollowingError`).
* **Controlled ESTOP Ramp-Down**:
  * Upon ESTOP, RT ramps steering to center ($0^\circ$) at a controlled rate of $20.0^\circ/\text{s}$ rather than instantly disconnecting, preventing vehicle rollover at speed.

---

## 4. Fault Classification Architecture

SYS implements a **Two-Mask Fault Architecture** to prevent transient warnings from corrupting latched safety states:

```
                          ┌───────────────────────────────────────────────┐
                          │               System Diagnoser                │
                          └───────┬───────────────────────────────┬───────┘
                                  │                               │
                                  ▼                               ▼
                   ┌─────────────────────────────┐ ┌─────────────────────────────┐
                   │     g_inhibit_reasons       │ │   g_latched_fault_reasons   │
                   │   Transient / Recoverable   │ │   Permanent Safety Latch    │
                   └──────────────┬──────────────┘ └──────────────┬──────────────┘
                                  │                               │
                                  ▼                               ▼
                         Clamps Mode to MANUAL             Clamps Mode to MANUAL
                         Drops 0x113 72V Power             Drops 0x113 72V Power
                         Auto-recovers via Hysteresis      Preserves Steering (SES)
                                                           Requires Physical Reset
```

### 4.1 Transient Inhibit Reasons (`InhibitReason`)

Transient inhibits indicate recoverable communication blips or operating limits. They do not blow fuses or latch ESTOP, but they prohibit autonomous driving and open traction contactors:

| Bit Mask | Identifier | Trigger Condition | System Action | Recovery Mechanism |
| :--- | :--- | :--- | :--- | :--- |
| `1u << 0` | `kInhibitMtrFbkLoss` | `0x206` missing $> 200\,\text{ms}$ | Revoke 72V power, drop `AUTO` | 3 consecutive healthy frames at 20 Hz |
| `1u << 1` | `kInhibitSebCommsLoss` | `0x721` missing $> 100\,\text{ms}$ | Revoke 72V power, drop `AUTO` | 3 consecutive healthy frames at 50 Hz |
| `1u << 2` | `kInhibitBrakeFollowing` | SEB stroke error $> 3.0\,\text{mm}$ | Inhibit traction propulsion | 3 consecutive healthy frames at 50 Hz |

### 4.2 Latched Safety Faults (`LatchedFaultReason`)

Latched faults represent confirmed hardware failures or safety contract violations. Once set, they require an authenticated reset:

| Bit Mask | Identifier | Trigger Condition | System Action | Clear Condition |
| :--- | :--- | :--- | :--- | :--- |
| `1u << 0` | `kLatchedBrakeFollowing` | SEB stroke error persists $\ge 500\,\text{ms}$ | Latched traction cut, steering preserved | Physical START btn / `0x114` reset |
| `1u << 1` | `kLatchedSebL3` | SEB reports Level 3 fatal error | Latched traction cut, steering preserved | Physical START btn / `0x114` reset |

---

## 5. Heartbeat & Authority Coordination

```
                  High CAN                                   Low CAN
 ┌────────────┐   0x7FC (10 Hz)   ┌────────────┐   0x7FD (10 Hz)   ┌────────────┐
 │ Host (AV)  ├──────────────────►│  rt-esp32   ├──────────────────►│ sys-esp32  │
 └────────────┘                   └──────┬─────┘                   └──────┬─────┘
                                         │         0x7FE (10 Hz)          │
                                         │◄───────────────────────────────┤
                                         │                                │
                                         ▼                                ▼
                                  [RT Watchdog]                    [SYS Watchdog]
                              Host loss: Drop AUTO            RT loss: Standstill / MANUAL
                              SYS loss: Zero setpoints        RT loss: Inhibit AUTO
```

### 5.1 Heartbeat Contracts & Timeout Policies
* **Host Heartbeat (`0x7FC`)**: Transmitted by Host at 10 Hz. RT checks freshness: if missing $> 1500\,\text{ms}$ in `AUTO`, RT triggers local motion shutdown.
* **RT Heartbeat (`0x7FD`)**: Transmitted by RT at 10 Hz. SYS checks freshness: if missing for 3 consecutive frames ($> 300\,\text{ms}$), SYS prohibits `AUTO` and commands safe standstill.
* **SYS Heartbeat (`0x7FE`)**: Transmitted by SYS at 10 Hz. RT checks freshness: if missing for 3 consecutive frames ($> 300\,\text{ms}$), RT revokes drive authority.

### 5.2 Boot Authority & ESTOP Clear Handshake
1. **Unacquired Authority at Boot**: RT initializes with `g_no_sys_authority = true`. Motion setpoints are forced to zero until SYS begins a healthy, uninterrupted stream of `0x011 SYS_SAFETY_STS` and `0x110 SYS_MODE_CMD`.
2. **Asymmetric ESTOP Release**: To exit ESTOP, RT requires **two consecutive clean `0x011` frames** with `estop_active == 0`. A single glitch frame cannot accidentally re-enable propulsion.
3. **Authenticated Remote ESTOP Reset (`0x114`)**: Host reset requests require verification token `0x5253` ('RS') and rolling counter progression. Reset is rejected if any of the 6 safety blockers are asserted (physical button active, latched fault asserted, measured vehicle speed $> 50\,\text{mm/s}$, heartbeat missing, MTR unacknowledged, or transient inhibit active).

---

## 6. Summary Comparison: Why Neither Controller Alone Is Sufficient

| Failure Mode | If Handled Only by SYS | If Handled Only by RT | Realized Dual-ECU Solution |
| :--- | :--- | :--- | :--- |
| **MTR Feedback Loss (`0x206` stale)** | SYS cuts 72V power at 20 Hz, but RT continues blasting 100 Hz drive commands for several cycles into a silent motor. | RT drops `0x204` drive commands, but cannot cut 72V contactors or stop manual throttle if motor controller faults. | **Both act**: RT cuts motion setpoints within 10 ms; SYS opens 72V power contactors and enforces mode inhibit. |
| **Brake Comms Loss (`0x721` / `0x7B9`)** | If SYS firmware stalls, no node would command the brake to stop the vehicle. | RT cannot arbitrate physical brake lever or manage vehicle power. | **Single writer with takeover**: SYS handles normal arbitration; RT takes over emergency `0x7B9` if SYS dies. |
| **Steering Linkage Jam** | SYS has no steering kinematics solver and cannot detect angle tracking divergence. | RT detects following error and zeros commands, but cannot revoke vehicle high-voltage power. | **RT detects and signals**: RT catches tracking error within 300 ms, initiates ramp-to-zero, and asserts CAN `0x001` ESTOP to SYS. |
| **Host Planning Stack Hang** | SYS does not receive High CAN and cannot detect Host software freezes directly. | RT detects `0x300` command staleness within 500 ms and halts motion. | **Gateway containment**: RT halts drive setpoints and steering, notifying SYS via `0x7FD` state reporting. |
| **Physical Button Pressed** | SYS cuts power, commands full brake, and broadcasts `0x001` immediately. | RT receives `0x001` and `0x110` to zero kinematics and steer to center. | **Direct hardware interlock**: Hard-wired fail-safe to SYS; instantaneous CAN broadcast to all nodes. |
