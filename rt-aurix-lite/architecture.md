# E-Trike System Architecture — Consolidated Realtime AURIX (RT-only)

**Three-node control:** **Jetson Orin** (ROS 2 perception/planning), **RT** — a single
**AURIX TC375** (AURIX™ Lite Kit V2) that merges realtime physics, steering, brake,
safety, body control, mode authority and the CAN gateway — and **MTR STM32** (motor
actuation, EGAS Level 1). There is **no separate SYS node**: RT absorbs all former SYS
safety/body duties, so **no RT↔SYS intercommunication exists anywhere in the system.**

## Hardware identity

| Property | Value |
|----------|-------|
| Evaluation board | **Infineon KIT_A2G_TC375_LITE** (AURIX™ lite Kit V2, Rev 2.2) |
| MCU | **SAK-TC375TP-96F300W AA**, LQFP-176 |
| Cores | **3 × TriCore** @ **300 MHz** (CPU0, CPU1, CPU2) |
| Lockstep | **CPU0 and CPU1 lockstep-protected; CPU2 not** |
| Program Flash | 6 MB |
| SRAM (incl. cache) | 1136 KB |
| CAN | **2 MCMCAN modules × 4 nodes each = 8 CAN nodes** |
| DMA | 128 channels |
| FPU / DSP | Yes |
| HSM | Yes (eVita) |
| Safety | up to **ASIL-D / SIL-3** hardware support |

Per-core memory (this exact derivative):

| Memory | CPU0 | CPU1 | CPU2 |
|--------|-----:|-----:|-----:|
| DSPR | 240 KB | 240 KB | 96 KB |
| DLMU | 64 KB | 64 KB | 64 KB |
| PSPR | 64 KB | 64 KB | 64 KB |
| Lockstep checker | Yes | Yes | No |

> **Sources:** board manual [`aurix.md`](aurix.md) (AURIX™ Lite Kit V2 Rev 2.2) for board
> pin/connector/power facts; TC37x datasheet and TC375 Safety Lite Kit documentation for
> the MCU capability table above. The on-board CAN transceiver is the **TLE9251VSJ** on
> CAN node 0 (`P20.7`/`P20.8`); the exact MCMCAN module/node assignments for **both** buses
> are confirmed during ADS/iLLD bring-up (§9.1).

This document is grounded in the board manual [`aurix.md`](aurix.md) (AURIX™ Lite Kit V2,
Rev 2.2, TC375) and the RT-only wire contracts in [`protocol/`](protocol/README.md), which
are generated from the repository's canonical [`protocol/`](../protocol/) contracts by the
same scripts.

> **Deployment status:** Architecture only. No RT source implementation exists yet for the
> AURIX target. Statements here are target design, not bench evidence. The implementation
> approach and phase gates are defined in [`work-plan.md`](work-plan.md).

---

## 1. Topology

Two physical CAN buses at 500 kbit/s. RT is the only dual-bus node and bridges selected
frames between them.

```
  ┌────────────────── High-Level CAN (500 kbit/s) ──────────────────┐
  │                                                                  │
  │  ┌──────────┐            ┌──────────────────┐                   │
  │  │  Jetson  │            │   RT  AURIX TC375 │                  │
  │  │  Orin    │            │                  │                  │
  │  │          │            │ Physics          │                  │
  │  │ ROS 2    │            │ Steering         │                  │
  │  │ Planning │            │ Brake            │                  │
  │  └────┬─────┘            │ Safety (ASIL)    │                  │
  │       │                  │ Body / Mode       │                  │
  │       │                  │ Gateway           │                  │
  │  TX:  0x300,0x301,       └──────┬───────────┘                   │
  │       0x302,0x303,0x400,        │                               │
  │       0x7FC,0x111,0x112         │                               │
  │  RX:  0x001,0x011,0x120,       TX: 0x001,0x011,0x120,0x206,     │
  │       0x206,0x210,0x220,        0x210,0x220,0x310,0x311,        │
  │       0x310,0x311,0x600,        0x400,0x600,0x7FD                │
  │       0x7FD,0x121                RX: 0x300,0x301,0x302,0x303,    │
  │                                 0x400,0x7FC,0x111,0x112          │
  └──────────────────────────────────────────────────────────────────┘
                                           │
                 ┌─────────────────────────┘
                 │
  ┌──────────────▼─────── Low-Level CAN (500 kbit/s) ───────────────┐
  │                                                                  │
  │  ┌──────────────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐  │
  │  │  RT AURIX TC375  │  │  EPS-C   │  │   SEB    │  │  MTR   │  │
  │  │                  │  │(Steering)│  │  (Brake) │  │ STM32  │  │
  │  │ TX: 0x001,0x110, │  │0x169 cmd │  │0x7B9 cmd │  │RX:0x110│  │
  │  │      0x169,0x204,│  │0x201 st  │  │0x721 st  │  │  0x204 │  │
  │  │      0x7B9,0x7FD │  │0x202 err │  │0x731 err │  │TX:0x120│  │
  │  │ RX: 0x001,0x120, │  │0x203 ver │  │0x741 ver │  │  0x206 │  │
  │  │      0x201,0x202,│  │0x6FA tst │  │0x6FB tst │  │RX:0x001│  │
  │  │      0x203,0x206,│  └──────────┘  └──────────┘  └────────┘  │
  │  │      0x6FA,0x6FB,0x721,0x731,0x741,0x7FD                     │
  │  └──────────────────────────────────────────────────────────────┘
  └──────────────────────────────────────────────────────────────────┘
```

> **Dual CAN hardware on RT:** the AURIX TC375 has multiple MCMCAN nodes. **MCMCAN0**
> drives the low-level bus (EPS-C, SEB, MTR) using the on-board TLE9251VSJ transceiver on
> `P20.7/P20.8` with standby control `P20.6`. **CAN_HIGH** drives the high-level bus
> (Jetson) via a second, external transceiver (part TBD — see §9.1.2) wired to the
> mikroBUS P15.0/P15.1 pins — see §9.1.

---

## 2. What Merging Eliminates (Intercommunication)

Because RT now owns everything that SYS once did, all RT↔SYS traffic and dual-ownership
logic disappear. This is the core difference from the distributed architecture.

### 2.1 Removed messages

| Removed message | Former role | Why it disappears |
| --- | --- | --- |
| `0x7FE` SYS_HEARTBEAT | SYS → RT liveness | SYS node gone; RT supervises itself via its own safety core + watchdog |
| `0x205` RT_BRAKE_CMD | RT → SYS brake setpoint | RT now drives `0x7B9` directly (no middle hop) |
| `0x012` SYS_DCDC_CMD / powertrain bus | DC-DC converter control | no powertrain bus in this variant; DC-DC is out of scope |
| `0x7FD` (low, receivers incl. SYS) | RT → SYS liveness | SYS gone; low-bus `0x7FD` now targets MTR only |

### 2.2 Re-homed messages (SYS → RT, owner `rta`)

| ID | New name | New sender / receivers |
| --- | --- | --- |
| `0x011` | `RTA_SAFETY_STS` | RT → Host (high bus only) |
| `0x110` | `RTA_MODE_CMD` | RT → MTR (low bus only) |
| `0x600` | `RTA_DIAG_RPT` | RT → Host (high bus only) |

### 2.3 Removed logic

| Distributed mechanism | Distributed behavior | Consolidated behavior |
| --- | --- | --- |
| **Mode-gated dual control** | RT commands steering+brake in AUTO; SYS commands brake in MANUAL/ESTOP | RT owns **both** actuators in all modes — single owner, no mode gating |
| **`0x7B9` dual-sender suppression** | SYS suppresses its `0x7B9` in AUTO while RT sends; 6-condition suppression logic; deadman re-arbitration | **Single sender** (`RT`) — suppression logic deleted entirely |
| **SYS → RT forwarding** | `0x011`/`0x600` forwarded SYS→RT→Jetson | RT generates telemetry itself (high bus) — no forward path |
| **SYS as mode authority** | SYS owned mode; RT followed via `0x110` | RT owns mode authority (physical buttons + HMI `0x111`) |
| **Cross-ECU heartbeat supervision** | RT monitored SYS `0x7FE`; SYS monitored RT `0x7FD` | Intra-controller: per-core task health + external watchdog |
| **`0x204` → SYS + MTR** | RT drive setpoint went to both SYS (EGAS L2) and MTR | `0x204` → MTR only; EGAS L2 runs on RT's safety core |
| **`0x302` lights → SYS** | Host lights forwarded to SYS for relay drive | RT drives light relays directly; `0x302` stays on high bus (Host→RT) |

> **Wire compatibility:** Every retained message keeps its exact CAN ID, DLC, byte order,
> signal layout, scaling, enums and vendor checksum/rolling-counter algorithms from the
> canonical contracts. Only network/sender/instance metadata changed — see
> [`protocol/`](protocol/README.md). The custom SES/SEB codecs and their language-neutral
> vectors are byte-identical.

---

## 3. CAN Message Catalog

The authoritative catalog is the generated subset under
[`protocol/generated/`](protocol/generated/) (C++, Python, TypeScript, CSV, DBC, docs).
Summary tables:

### 3.1 Low-Level CAN (500 kbit/s) — nodes: RT, EPS-C, SEB, MTR

| ID | Name | Sender | Receivers | DLC | Rate |
|----|------|--------|-----------|-----|------|
| `0x001` | SAFETY_ESTOP | Any | RT, MTR | 0 | Event |
| `0x110` | RTA_MODE_CMD | RT | MTR | 1 | 1 s / change |
| `0x111` | HMI_MODE_REQ | HMI | RT | 2 | 1 s |
| `0x112` | HMI_PWR_REQ | HMI | RT | 2 | 1 s |
| `0x120` | SYS_THROTTLE_STS | MTR | RT | 2 | 100 Hz |
| `0x169` | VCU_SES_REQ | RT | EPS_C | 8 | 50 Hz |
| `0x201` | SES_STATUS | EPS_C | RT | 8 | 100 Hz |
| `0x202` | SES_ERR_INFO | EPS_C | RT | 8 | 10 Hz |
| `0x203` | SES_VERSION | EPS_C | RT | 8 | 1 Hz |
| `0x204` | RTA_DRIVE_CMD | RT | MTR | 5 | 100 Hz |
| `0x206` | MTR_MOTOR_FBK | MTR | RT | 4 | 50 Hz |
| `0x302` | HOST_LIGHT_CMD | Host (fwd) | RT | 1 | change |
| `0x6FA` | SES_TEST | EPS_C | RT | 8 | 100 Hz |
| `0x6FB` | SEB_TEST | SEB | RT | 8 | 100 Hz |
| `0x721` | SEB_STATUS | SEB | RT | 8 | 100 Hz |
| `0x731` | SEB_ERR_INFO | SEB | RT | 8 | 10 Hz |
| `0x741` | SEB_VERSION | SEB | RT | 8 | 1 Hz |
| `0x7B9` | VCU_SEB_REQ | RT | SEB | 8 | 50 Hz |
| `0x7FD` | RTA_HEARTBEAT | RT | MTR | 2 | 2 Hz |

### 3.2 High-Level CAN (500 kbit/s) — nodes: Jetson, RT, HMI

| ID | Name | Sender | Receivers | DLC | Rate |
|----|------|--------|-----------|-----|------|
| `0x001` | SAFETY_ESTOP | Any | RT, Host, MTR | 0 | Event |
| `0x011` | RTA_SAFETY_STS | RT | Host | 3 | 5 Hz |
| `0x111` | HMI_MODE_REQ | HMI | RT, Host | 2 | 1 s |
| `0x112` | HMI_PWR_REQ | HMI | RT | 2 | 1 s |
| `0x120` | SYS_THROTTLE_STS | MTR (fwd) | Host | 2 | 100 Hz |
| `0x121` | RTA_MOTION_RPT | RT | Host | 8 | 100 Hz |
| `0x206` | MTR_MOTOR_FBK | MTR (fwd) | Host | 4 | 50 Hz |
| `0x210` | RTA_STATE_RPT | RT | Host | 6 | 10 Hz |
| `0x220` | RTA_PID_RPT | RT | Host | 6 | reserved |
| `0x300` | HOST_DRIVE_CMD | Host | RT | 8 | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQ | Host | RT | 4 | demand |
| `0x302` | HOST_LIGHT_CMD | Host | RT | 1 | change |
| `0x303` | HOST_STEER_CMD | Host | RT | 4 | 100 Hz |
| `0x310` | STEER_DIAG | RT | Host | 8 | 10 Hz |
| `0x311` | BRAKE_DIAG | RT | Host | 8 | 10 Hz |
| `0x400` | HOST_OBSTACLE_DIST | Host | RT | 4 | 10 Hz |
| `0x600` | RTA_DIAG_RPT | RT | Host | 8 | 1 Hz |
| `0x7FC` | HOST_HEARTBEAT | Host | RT | 2 | 2 Hz |
| `0x7FD` | RTA_HEARTBEAT | RT | Host | 2 | 2 Hz |

### 3.3 RT CAN Gateway — Forwarding Rules

**Category 1 — transparent forward (same ID, same payload):**

| Direction | IDs |
|-----------|-----|
| Low → High | `0x001`, `0x120`, `0x206` |
| High → Low | `0x001`, `0x302` |

**Category 2 — consumed by RT → different message generated:**

| Inbound | Bus | Outbound | Bus |
|---------|-----|----------|-----|
| `0x300` HOST_DRIVE_CMD | High | `0x204` RTA_DRIVE_CMD + `0x169` VCU_SES_REQ | Low |
| `0x301` HOST_BRAKE_REQ | High | → brake arbitration → `0x7B9` VCU_SEB_REQ | Low |
| `0x303` HOST_STEER_CMD | High | → steering arbitration → `0x169` VCU_SES_REQ | Low |

**Category 3 — bus-local (never forwarded, never regenerated):**

| Bus | IDs |
|-----|-----|
| Low only | `0x110`, `0x169`, `0x201`, `0x202`, `0x203`, `0x204`, `0x6FA`, `0x6FB`, `0x721`, `0x731`, `0x741`, `0x7B9` |
| High only | `0x011`, `0x121`, `0x210`, `0x220`, `0x310`, `0x311`, `0x600` (RT telemetry) |
| Both independent | `0x7FD` (RT heartbeat, per-bus, NOT bridged), `0x7FC` (Host, high only) |

> Because `0x011`/`0x600`/`0x120`/`0x206` are now RT-originated on the high bus or forwarded
> as MTR frames, they are **not** re-forwarded by the gateway — there is no SYS node to feed.

---

## 4. Responsibility Split

| Concern | Jetson | RT (AURIX TC375) | MTR |
|---------|:------:|:-----:|:---:|
| Perception / planning | ✓ | | |
| ROS 2 → CAN bridge | ✓ | | |
| CAN gateway (low ↔ high) | | ✓ | |
| Tricycle kinematics | | ✓ | |
| Steering angle compute + CAN TX (0x169) | | ✓ | |
| Steering boot sync (Listen-Before-Speaking) | | ✓ | |
| Steering safety: dynamic angle clamp, hard stops, following error | | ✓ | |
| Obstacle speed limit | | ✓ | |
| Command staleness watchdog | | ✓ | |
| **E-stop GPIO + handling** | | ✓ | ✓ |
| **Mode authority + HMI requests** | | ✓ | |
| Brake lever → CAN (0x7B9, 50 Hz continuous) | | ✓ | |
| Brake boot sync + rolling counter + checksum | | ✓ | |
| Light relays + indicators + 12V accessory relay | | ✓ | |
| System diagnostics (0x600) | | ✓ | |
| **EGAS Level 2 monitor** | | ✓ | |
| Throttle MCP4725 DAC output (0–5 V) | | | ✓ |
| Gear 72 V output (relay module) | | | ✓ |
| Motor feedback CAN TX (0x206) | | | ✓ |
| **EGAS Level 1 function controller** | | | ✓ |

---

## 5. Mode and ESTOP State Machine

Mode authority is **RT** (physical buttons on the AURIX board + HMI `0x111`). ESTOP is a
safety state overlaid on the current mode, triggered by the hardware ESTOP button, CAN
`0x001`, or a safety fault — never by the mode button.

```
MANUAL ←→ AUTO       (mode button / HMI 0x111)
   ↓       ↓
  ESTOP ←────────── (hardware button, CAN 0x001, safety faults)
   |
   └──START button──→ MANUAL
   └──MODE long-press─→ MANUAL
```

| State | Behavior |
|------|----------|
| **MANUAL** | Rider steers; EPS-C standalone (RT monitors `0x201`). Brake lever → RT GPIO → `0x7B9` (Stroke mode 15 mm/0 mm). MTR pass-through from grip/gear. RT keeps lights on. |
| **AUTO** | Jetson `0x300` → RT kinematics → `0x204` (MTR) + `0x169` (EPS-C). Brake via `0x7B9` (Pressure mode from arbitration; stroke override for lever). Lights from Jetson `0x302`. Optional `0x303` direct steer command → arbitration. |
| **ESTOP** | Steering ramps to 0° at 20°/s via active `0x169`; `0x7B9` stroke=max (27 mm). MTR kills locally (DAC=0, gear OFF). `0x001` broadcast both buses. 12V accessory relay OFF. Brake light ON. DC-DC: N/A (no powertrain bus). Exit: START button or MODE long-press → MANUAL. |

---

## 6. Runtime Model (TC375 — target-gated)

The AURIX TC375 has **three TriCore cores**. This document specifies the **functional
partition** across cores and the **required execution periods**; it deliberately does **not**
commit to a scheduler topology (FreeRTOS kernels vs. deterministic cyclic executives vs. a
mix) until the target-early feasibility spike decides (see [`work-plan.md`](work-plan.md)
target gate).

> **Correction:** for the exact part `SAK-TC375TP-96F300W AA`, **CPU0 and CPU1 are
> lockstep-protected; CPU2 is not**. Earlier text claiming "TC375 has no lockstep" was
> wrong (that applies to some TC3xx family members, not this part). Freedom-from-interference
> additionally uses AURIX **MPU** (memory-protection sets) and **SMU**.

### 6.1 Functional partition (core-affinity intent)

| Core | Domain | Functional units | Owns |
|------|--------|------------------|------|
| **CPU0** (master, lockstep) | Data plane / gateway (QM) | `can_rx_low`, `can_rx_high`, `dispatch`, `can_tx_low`, `can_tx_high`, `heartbeat` | CAN_LOW bus, CAN_HIGH bus, CAN transceivers, gateway queues |
| **CPU1** (lockstep) | Motion control + safety (**ASIL**) | `safety`, `control`, `brake`, `watchdog` | ESTOP GPIO, TPS3850-Q1 WDT, actuator setpoint/feedback, SMU init, steering/brake state machines |
| **CPU2** (non-lockstep) | Body + HMI + diag (QM) | `lights`, `mode`, `indicator`, `power`, `diag` | light relays, mode/START buttons, HMI `0x111`/`0x112`, 12V relay |

> **Why CPU0/CPU1 for safety:** both are lockstep-protected, so the safety-critical domain
> can be isolated on a lockstep core. CPU2 (non-lockstep) carries only QM body/HMI work.
> The exact core that owns the MCMCAN peripheral(s) and the ESTOP emergency resource is a
> **bring-up decision** (§6.2).

### 6.2 Runtime mechanism — decision gate (deferred)

The following are all candidates; the winner is chosen only after the target spike
([`work-plan.md`](work-plan.md) target gate) demonstrates multicore startup, peripheral
access, shared-memory IPC and synchronization:

- **Three independent FreeRTOS kernels (AMP)** — one per core, tasks pinned.
- **One/two FreeRTOS kernels + deterministic cyclic executive(s)** on the remaining core(s).
- **Deterministic cyclic executives only** (no RTOS) with interrupt/event handling.

> **Rule:** *do not port the ESP32 firmware's execution architecture. Port its required
> behavior.* The execution architecture belongs to the TC375 and remains unresolved until
> target bring-up. No 15-FreeRTOS-task shell is built on the host.

### 6.3 Functional units and required periods

The **15 functional units** below are the work to be performed (regardless of runtime
mechanism). Periods are the required execution cadence; on a cyclic executive these are
slot periods, on FreeRTOS these are task periods.

| Unit | Core | Prio | Period | Behavior |
|------|------|------|--------|----------|
| `can_rx_low` | 0 | 5 | event | CAN_LOW → RX queue |
| `can_rx_high` | 0 | 5 | event | CAN_HIGH → RX queue |
| `dispatch` | 0 | 4 | event | route both RX queues + gateway + steering/brake feedback + fault escalation |
| `can_tx_low` | 0 | 3 | event | `0x204`@100 Hz, `0x169`@50 Hz, `0x7B9`@50 Hz, `0x110`, gateway forwards |
| `can_tx_high` | 0 | 3 | event | `0x011`, `0x121`, `0x210`, `0x310`, `0x311`, `0x600`, forwarded `0x120`/`0x206` |
| `heartbeat` | 0 | 1 | 2 Hz | `0x7FD` on both buses (per-bus, not bridged) |
| `safety` | 1 | 5 | 20 Hz | ESTOP GPIO, MTR liveness (`0x206`), EGAS L2 comparison, SMU monitoring |
| `control` | 1 | 4 | 100 Hz | kinematics, dynamic angle clamp, obstacle limit, brake arbitration, safety checks, ESTOP handling |
| `brake` | 1 | 3 | 50 Hz | SEB boot sequence + `0x7B9` continuous TX + lever |
| `watchdog` | 1 | 1 | 10 Hz | command staleness (500 ms) → zero setpoints + stop steer; watchdog health decision |
| `lights` | 2 | 3 | 20 Hz | turn/brake/head lamp GPIOs + blink |
| `mode` | 2 | 4 | 10 Hz | MODE/START buttons, HMI `0x111`, ESTOP exit, `0x110` to MTR |
| `indicator` | 2 | 2 | 5 Hz | mode bulbs (AUTO/MANUAL) |
| `power` | 2 | 2 | 5 Hz | 12V accessory relay |
| `diag` | 2 | 1 | 1 Hz | system health → `0x600` |

### 6.4 Cross-Core IPC

"Queues over shared state" extends across cores. No mutexes in the data path:

- **Lock-free single-writer / single-reader ring buffers** per core-to-core link, with
  CPU-to-CPU interrupts (SRE/service request) for wakeup.
- **Atomic pipeline across cores:** RX decodes → typed inputs → `control` computes physics
  → setpoint outputs → TX sends `0x204`/`0x169`/`0x7B9`.
- **ESTOP bypass:** the safety domain raises an **urgent** transmit (`TxClass::Urgent`); the
  target implements the emergency resource per the proven architecture (direct mailbox write
  vs. other). The portable interface does **not** expose mailbox details.
- **Mode authority:** the body domain publishes mode; motion/control and TX consume it;
  heartbeat aggregates per-core health into `0x7FD`.
- **Target correctness (later):** shared-memory placement (LMU/DLMU), alignment, publication
  ordering and **DSYNC**/compiler barriers, SRI routing, and MPU/BMP access are established
  during the target spike — not simulated on the host.

---

## 7. EGAS 3-Level Motor Safety

```
Level 3: Hardware — ESTOP button wired direct to both RT and MTR
         TPS3850-Q1 external watchdog. No software, no CAN.
         ESTOP press → MTR cuts throttle + gear instantly (local).

Level 2: Function Monitor — RT CPU1 safety unit (ASIL, prio 5)
         Isolated on a lockstep core (CPU1); AURIX MPU (memory-protection
         sets) isolates safety memory from QM cores; SMU enforces
         freedom-from-interference. Compares 0x204 setpoint vs 0x206
         feedback. Mismatch > 500 mm/s for > 500 ms → CAN 0x001 ESTOP.

Level 1: Function Controller — MTR STM32
         Reads sensors, drives MCP4725 DAC + gear relays.
         MANUAL: pass-through. AUTO: follows CAN 0x204.
         No wireless, no OS, minimal attack surface.
```

---

## 8. Heartbeat — Liveness Supervision

| ID | Sender | Bus | Receiver | Period | Timeout | Action on loss |
|----|--------|-----|----------|--------|---------|----------------|
| `0x7FD` | RT | Low | MTR | 2 Hz | 200 ms | MTR local fallback (maintain last safe speed, log) |
| `0x7FD` | RT | High | Jetson | 2 Hz | 1500 ms | Jetson stops publishing `/cmd_vel` |
| `0x7FC` | Jetson | High | RT | 2 Hz | 1500 ms | Assisted stop: zero `0x204`, stop `0x169`, `0x7B9` brake=2000 kPa |
| `0x206` | MTR | Low | RT | 50 Hz | 200 ms | `0x206` staleness — log warning (motor feedback lost) |

- MTR heartbeat is **implicit** — `0x206` at 50 Hz is the liveness signal; no separate MTR
  heartbeat ID.
- `0x7FD` is sent on **both buses independently** (per-bus alive counters, NOT bridged).
- `0x7FE` SYS heartbeat is **deleted** (no SYS node). RT's own liveness is internal:
  per-core health counters reported via `0x210`/`0x600`, plus the TPS3850-Q1 external
  watchdog serviced by the CPU1 safety unit.

---

## 9. Hardware Pin Assignments — AURIX TC375 (Lite Kit V2)

Pin facts from [`aurix.md`](aurix.md). Pins marked *(header)* are free board pins chosen
for this design; verify alternate functions against the TC375 datasheet.

### 9.1 CAN

The exact MCU has **2 MCMCAN modules × 4 CAN nodes each (8 nodes total)**. The module/node
assignments are now **frozen from the iLLD `IfxCan_PinMap_TC37x_LQFP176`**:

| Bus | Module | Node | TX | RX |
|-----|--------|------|----|----|
| CAN_LOW | CAN0 | Node 0 | `IfxCan_TXD00_P20_8_OUT` (P20.8, alt5) | `IfxCan_RXD00B_P20_7_IN` (P20.7, RxSel_b) |
| CAN_HIGH | CAN0 | Node 2 | `IfxCan_TXD02_P15_0_OUT` (P15.0, alt5) | `IfxCan_RXD02A_P15_1_IN` (P15.1, RxSel_a) |

The on-board Lite Kit transceiver is on **CAN0 Node 0** (`P20.7`/`P20.8`, `P20.6` standby).
The high bus uses **CAN0 Node 2** on `P15.0`/`P15.1` (TXCAN2/RXCAN2), exposed on mikroBUS
13/14. The `IfxCan_*Pin` symbols are taken from the iLLD `IfxCan_PinMap_TC37x_LQFP176`
(LQFP-176 = TC375 package).

| Bus | TX | RX | Standby | Transceiver | Status |
|-----|----|----|---------|-------------|--------|
| CAN_LOW | `P20.8` | `P20.7` | `P20.6` (`CAN_STB`, drive LOW to enable) | On-board **TLE9251VSJ** (120 Ω termination on board) | BOARD-FIXED per `aurix.md` Table 5 |
| CAN_HIGH | `P15.0` / TXCAN2 | `P15.1` / RXCAN2 | n/a | **TBD** (external; requirements in §9.1.2) | DATASHEET-VERIFIED / DESIGN-SELECTED |

CAN_HIGH property certainty (split per-property rather than one row-level status):

| Property | Value | Status |
|----------|-------|--------|
| Logical bus | `CAN_HIGH` | DESIGN-SELECTED |
| TX pin | `P15.0` / TXCAN2 | DATASHEET-VERIFIED |
| RX pin | `P15.1` / RXCAN2 | DATASHEET-VERIFIED |
| Connector | mikroBUS pin 13 (TX) / pin 14 (RX) | BOARD-VERIFIED |
| Intended use | high-level CAN (Jetson) | DESIGN-SELECTED |
| MCMCAN module/node | **CAN0 Node 2** (`IfxCan_TXD02_P15_0_OUT` / `IfxCan_RXD02A_P15_1_IN`) | DATASHEET-VERIFIED (iLLD pinmap) |
| iLLD `IfxCan_*Pin` symbols | `IfxCan_TXD02_P15_0_OUT`, `IfxCan_RXD02A_P15_1_IN` | DATASHEET-VERIFIED |

> **CAN_HIGH pins:** the TC37x datasheet assigns TXCAN2/RXCAN2 to P15.0/P15.1, which the
> Lite Kit V2 exposes on the mikroBUS TX/RX pins (13/14). The prior proposal
> (`P15.5`/`P15.4` on X1-29/31) is **removed** — neither pin has a CAN alternate function on
> TC37x. Using P15.0/P15.1 for CAN_HIGH **sacrifices the mikroBUS ASCLIN1 UART**. The exact
> MCMCAN module/node and iLLD pin-mapping symbols are confirmed during ADS/iLLD bring-up.

> The Lite Kit V2 has exactly **one on-board CAN transceiver** (CAN node 0). The high bus
> requires an external transceiver on the expansion header.

#### 9.1.1 CAN termination rule

**Exactly two 120 Ω terminations across CANH–CANL, at the two physical ends of each CAN
bus.**

| Bus | Termination points |
|-----|--------------------|
| CAN_LOW | Lite Kit on-board termination: **120 Ω on-board**. If the Lite Kit is at one physical bus end, one external endpoint termination is still required at the far end. |
| CAN_HIGH | External-transceiver PCB termination: **configurable / DNP by default**. Actual termination depends on the physical bus position of the two endpoints. |

#### 9.1.2 CAN_HIGH transceiver (TBD)

Part: **TBD**. The generic `SN65HVD230 (or TLE9251V)` pairing is not used; requirements only:

- ISO 11898-2 high-speed CAN.
- 500 kbit/s required.
- MCU-side interface compatible with TC375 I/O (3.3 V).
- Automotive-qualified preferred.
- Standby/enable behavior explicitly defined.
- External CANH/CANL.
- Termination configurable according to bus position.

When the component is selected, add its exact VCC/VIO arrangement, TXD/RXD levels, STB/EN
pin, decoupling, protection, termination and connector wiring.

### 9.2 Rider inputs / body outputs (free board pins)

| Signal | AURIX pin | Dir | Notes |
|--------|-----------|-----|-------|
| ESTOP_BTN | `P00.0` *(header X2-3)* | In (pull-up) | NC, active-low. Shared with MTR hardware ESTOP |
| BRAKE_LEVER | `P00.1` *(header X2-4)* | In (pull-up) | Active-low |
| START_BTN | `P00.2` *(header X2-5)* | In (pull-up) | Exits ESTOP |
| MODE_BTN | `P00.3` *(header X2-6)* | In (pull-up) | MANUAL ↔ AUTO toggle |
| SW_LEFT_TURN | `P00.8` *(header X2-9)* | In (pull-up) | handlebar switch |
| SW_RIGHT_TURN | `P00.10` *(header X2-11)* | In (pull-up) | handlebar switch |
| SW_HEADLIGHT | `P00.11` *(header X2-14)* | In (pull-up) | toggle |
| LIGHT_LEFT | `P33.10` *(header X2-38)* | Out | relay → 12 V lamp |
| LIGHT_RIGHT | `P33.11` *(header X1-3)* | Out | relay → 12 V lamp |
| BRAKE_LIGHT | `P33.12` *(header X1-4)* | Out | relay → 12 V lamp |
| HEADLIGHT | `P33.13` *(header X1-5)* | Out | relay → 12 V lamp |
| BULB_AUTO | `P21.4` *(header X1-19)* | Out | mode indicator |
| BULB_MANUAL | `P21.5` *(header X1-22)* | Out | mode indicator |
| RELAY_12V | `P21.0` *(header X1-15)* | Out | 12 V accessory relay (P21.0 is free) |
| WDT_WDI | `P33.1` *(header X2-29)* | Out | TPS3850-Q1 WDI. **DESIGN-SELECTED**; bring-up: verify electrical level + watchdog window timing. (Prior `P20.9`/X1-28 claim was incorrect — X1-28 = `P20.14`; `P20.9` is package pin 127, flash INT path, not exposed on X1.) |

> **Added external components (not on the Lite Kit V2):**
> - Second CAN transceiver (high bus).
> - TPS3850-Q1 external watchdog (EGAS L3). If omitted, rely on AURIX internal SMU + per-core
>   task watchdogs (documented as reduced safety depth).
> - Light/relay driver board (the board exposes GPIO only; no high-current relay drivers).
> - ESTOP button and rider switches are wired to free header pins (the board's own Button1
>   `P00.7` and LED1/LED2 `P00.5`/`P00.6` remain available as debug/user I/O).

### 9.3 On-board resources used

- **LED1/LED2** (`P00.5`/`P00.6`) — user/debug LEDs (not required by this design).
- **Button1** (`P00.7`) — user push-button (debug use).
- **Potentiometer R32 → AN0** — spare analog input (or remove R33 to free AN0).
- **I²C EEPROM 24AA02E48 (address 0x50)** on I²C0 (`P13.1`/`P13.2`) — EUI-48 MAC for
  Ethernet; not used by this CAN-only design but available.
- **Ethernet DP83825I** (RMII, P11.x, MDC/MDIO P21.2/P21.3, INT_ETH P33.7) — unused by this
  design; pins remain free unless repurposed.
- **Optional Semper flash (P22.0–P22.3) / F-RAM (P23.1)** — unused; QSPI pins remain free.
- **`P14.0`/`P14.1`** are ASCLIN0 USB-serial (miniWiggler) — **not** available for CAN.

---

## 10. Error Responses

| Failure | Detection | Response |
|---------|-----------|----------|
| ESTOP GPIO (hardware) | `P00.0` LOW (NC) | Immediate: mode→ESTOP, `0x001` both buses, `0x7B9` max, steer ramp-to-zero |
| Host heartbeat timeout (1500 ms) | frozen counter on `0x7FC` | Zero drive + assisted-stop brake (2000 kPa); mode stays AUTO |
| MTR liveness timeout (200 ms) | no `0x206` arrival | Zero speed setpoint + force Neutral; log; `brake_fault` flag |
| EGAS L2 speed mismatch | \|`0x204` − `0x206`\| > 500 mm/s for 500 ms | ESTOP via `0x001` |
| Steering follow-error | \|cmd − actual\| > threshold for 300 ms | ESTOP via `0x001` both buses |
| EPS-C angle implausible | >30° off center at boot sync | Refuse ACTIVE → FAULT |
| EPS-C L3 fault (`0x202`) | L3 fault bits | ESTOP via `0x001` |
| SEB L3 fault (`0x731`) | L3 fault bits | ESTOP via `0x001` |
| `0x721`/`0x201` checksum fail | XOR(bytes 0–6) ^ 0xFF mismatch | Drop frame (checksum-before-L3 pattern) |
| Command stale (500 ms) | `watchdog` unit | Zero `0x204` + steering ramp-to-zero |
| CAN bus-off (low) | TEC poll / error interrupt | Auto-recover; 5 consecutive → ESTOP (loss of actuator bus non-survivable) |
| CAN bus-off (high) | TEC poll / error interrupt | Graceful: zero setpoints, steer ramp-to-zero (Jetson link loss survivable) |
| Unit stalled >500 ms | per-core health counters (CPU0/CPU2) + safety unit (CPU1) | Log ERROR; TPS3850-Q1 external WDT as backstop |

### 10.1 Asymmetric Bus-Off Response

| Bus | 5× bus-off response | Rationale |
|-----|---------------------|-----------|
| Low | Full ESTOP: `0x001` both buses | Loss of actuator bus (EPS-C/SEB/MTR) is non-survivable |
| High | Graceful: zero setpoints, steer ramp-to-zero | Loss of Jetson link is survivable |

### 10.2 Checksum-Before-L3

For `0x201` (steering) and `0x721` (brake) status frames, L3 error evaluation happens
**after** checksum validation. A corrupt frame with noise flipping error bits is rejected by
checksum before L3 escalation. DLC < 8 → immediate reject.

---

## 11. Differences vs. Distributed Architecture

| Aspect | Distributed (RT ESP32 + SYS ESP32) | Consolidated (RT AURIX TC375) |
|--------|-----------------------------------|-------------------------------|
| MCUs | 2× ESP32-S3 (RT + SYS) | 1× AURIX TC375 (RT) + MTR |
| SYS node | separate | **removed** (folded into RT) |
| CAN buses | 2 (high + low) | 2 (same) |
| CAN gateway | RT ESP32 bridges | RT AURIX bridges |
| RT↔SYS messages | `0x7FE`, `0x7FD`(low→SYS), `0x205`, `0x110`(RT←SYS), `0x011`/`0x600`(fwd) | **none** — all removed/re-homed |
| `0x7B9` owner | SYS (MANUAL/ESTOP) / RT (AUTO), dual-sender suppression | RT in all modes, single sender |
| Mode authority | SYS | RT |
| EGAS Level 2 | SYS ESP32 (separate MCU) | RT CPU1 safety unit (lockstep core, MPU + SMU) |
| Freedom-from-interference | physical (separate MCU) | logical (CPU0/CPU1 lockstep + MPU + SMU; CPU2 non-lockstep QM) |
| Cores | 2 single-core ESP32 | 3 TriCore (CPU0 data plane / CPU1 ASIL lockstep / CPU2 body) |
| RTOS / executors | 8 (RT) + 15 (SYS) = 23 tasks | 15 functional units; runtime mechanism **target-gated** (not pre-committed to FreeRTOS) |
| Powertrain bus / DC-DC | present (PWT) | **dropped** (no powertrain bus) |
| BOM cost | higher (2 MCUs + 2 transceivers + powertrain) | lower (1 MCU + 2 transceivers, no PWT) |
| Development complexity | cross-MCU coordination | single codebase, 3-core partition |

---

## 12. Configuration Constants

The firmware configuration is split by concern (implemented under `src/config/`):

- `control_config.h` — physics/control constants (reuse `shared_config.h` where identical).
- `safety_config.h` — safety thresholds (EGAS, ESTOP, faults).
- `timing_config.h` — periods, timeouts, heartbeat (§8).
- Board pins (`P15.0`, `P15.1`, `P33.1`, …) belong to the future `platform/aurix/board_pins.h`
  (or the board HAL), **not** firmware configuration.

Representative values (namespace `rta`):

```cpp
namespace rta {

// ── Vehicle (shared) ─────────────────────────────────────────────
// Use shared:: constants from shared/shared_config.h where available.

// ── Steering (steer-by-wire unit) ────────────────────────────────
constexpr float kSteerHardLimitDeg     = 40.0f;
constexpr float kSteerFollowingErrDeg  = 5.0f;
constexpr int   kSteerFollowingErrMs   = 300;
constexpr int   kSteerCmdRateHz        = 50;
constexpr int   kSteerBootWaitMs       = 500;
constexpr float kSteerMaxAngleLowSpeed = 40.0f;
constexpr float kSteerMaxAngleHighSpeed= 5.0f;

// ── Brake (brake-by-wire unit) ───────────────────────────────────
constexpr int   kBrakeCmdRateHz  = 50;
constexpr int   kBrakeBootWaitMs = 500;
constexpr float kBrakeManualStroke = 15.0f;
constexpr float kBrakeMaxStroke    = 27.0f;

// ── PID (shadow, future active) ──────────────────────────────────
constexpr float kPidKp = 1.0f, kPidKi = 0.1f, kPidKd = 0.05f;

// ── Timing ───────────────────────────────────────────────────────
constexpr int kControlLoopHz      = 100;
constexpr int kHeartbeatId        = 0x7FD;
constexpr int kHeartbeatIntervalMs= 500;
constexpr int kHeartbeatTimeoutMsMtr    = 200;
constexpr int kHeartbeatTimeoutMsJetson = 1500;

// ── CAN ──────────────────────────────────────────────────────────
constexpr int kCanLowBitrateHz  = 500000;
constexpr int kCanHighBitrateHz = 500000;

// ── GPIO (AURIX Lite Kit V2) ─────────────────────────────────────
constexpr int kEstopGpio     = 0;   // P00.0
constexpr int kBrakeLeverGpio= 1;   // P00.1
constexpr int kStartBtnGpio  = 2;   // P00.2
constexpr int kModeBtnGpio   = 3;   // P00.3
constexpr int kSwLeftTurnGpio= 8;   // P00.8
constexpr int kSwRightTurnGpio=10;  // P00.10
constexpr int kSwHeadlightGpio=11;  // P00.11
constexpr int kLightLeftGpio = 80;  // P33.10
constexpr int kLightRightGpio= 81;  // P33.11
constexpr int kBrakeLightGpio= 82;  // P33.12
constexpr int kHeadlightGpio = 83;  // P33.13
constexpr int kBulbAutoGpio  = 84;  // P21.4
constexpr int kBulbManualGpio= 85;  // P21.5
constexpr int kWdtToggleGpio = 86;  // P33.1 (X2-29), TPS3850-Q1 WDI
// CAN: MCMCAN0 P20.8 TX / P20.7 RX / P20.6 STB (board-fixed)
//      CAN_HIGH P15.0 TXCAN2 / P15.1 RXCAN2 (mikroBUS 13/14; MCMCAN module TBD)
// WDT:  P33.1 (X2-29) -> TPS3850-Q1 WDI (window timing per TPS3850-Q1 config)

} // namespace rta
```

---

## 13. Protocol Generation Workflow

The RT-only CAN layer is a **parallel, self-contained protocol tree** generated by its own
tool — the shared `protocol/tools/` is **not modified** and the canonical RT/SYS workflow is
untouched. See [`protocol/README.md`](protocol/README.md).

```bash
# Validate
python rt-aurix-lite/protocol/tools/generate.py validate

# Generate codecs / manifests / Python / TypeScript / C++
python rt-aurix-lite/protocol/tools/generate.py generate

# Generate derivative artifacts (DBC, CSV, Markdown docs)
python rt-aurix-lite/protocol/tools/generate.py derive
```

Generated hashes (subset): `SEMANTIC_HASH=e46be1e489de29116d0661ca908f242209a60fb0b66887382032c0274025ff03`,
`NETWORK_HASH=8ac1b59c1b5605c5472fd07eef968da569f249b3b0e29e7229e296d2d4557aa5`.

---

## 14. References

- [`aurix.md`](aurix.md) — AURIX™ Lite Kit V2 board manual (TC375, Rev 2.2) — pin facts, power, CAN0, connectors.
- [`protocol/README.md`](protocol/README.md) — RT-only stripped protocol subset and regeneration.
- [`protocol/`](protocol/) — generated C++/Python/TS codecs, DBC, CSV, docs, manifests.
- [`can-dictionary.md`](../can-dictionary.md) — canonical bit-level signal layouts (wire layouts identical).
- [`architecture.md`](../architecture.md) — distributed reference architecture (RT + SYS) this variant consolidates.
- [`wiring.md`](wiring.md) — harness/wiring reference (status-coded connections).
- [`work-plan.md`](work-plan.md) — phased implementation plan, exit gates, and cleanup rules.

---

## 15. Implementation Strategy

> **Principle:** *Do not port the ESP32 firmware's execution architecture. Port its required
> behavior. The execution architecture belongs to the TC375 and remains unresolved until
> target bring-up.*

### 15.1 Host-first, target-early

- **Host now:** implement the scheduler-independent control, safety, protocol and
  state-machine logic as platform-agnostic C++ (`src/`), validated with a **deterministic
  three-domain system simulation** (virtual CPU0/CPU1/CPU2 executors + virtual CAN/GPIO/
  watchdog + fault injection) on the existing `native-test` harness.
- **Target gate:** install the TriCore toolchain + TC375 iLLD, then prove a walking skeleton
  (multicore startup, real CAN_LOW/CAN_HIGH, shared-memory IPC + DSYNC, SRI, MPU/SMU)
  **before** choosing the runtime mechanism.
- **Do not** build 15 FreeRTOS task shells on the host; the runtime is decided at the gate.

### 15.2 Layered source tree (dependency direction)

```
CAN bytes → generated codec → protocol adapter → typed input
        → app orchestration → domain logic → typed output
        → protocol adapter → generated codec → CAN bytes
```

- `domain/` — pure logic: no CAN IDs, no IPC, no logging, no clock calls (`step(now_us, …)`).
- `app/` — orchestration controllers (motion, safety, body, gateway, watchdog-health).
- `protocol/` — decode/encode adapters + route table (owns all CAN-ID↔typed mapping).
- `ipc/` — snapshots, SPSC channels, typed messages (host: `std::atomic`; target: LMU/DSYNC later).
- `hal/` — interfaces only: `Can` (with `TxClass::Urgent`), `Gpio`, `Clock`, `Watchdog::service()`.
- `config/` — control/safety/timing split; board pins live in the board HAL, not config.
- `platform/` — `host/` (virtual adapters) now; `aurix/` (iLLD) after the gate.

See [`work-plan.md`](work-plan.md) for the phased plan, exit gates, and vertical commit sequence.
