# Diagnostic Event Plane (Phase B)

This document describes the diagnostic architecture for the E-Trike vehicle
network. It **supplements** existing safety/control detectors with one coherent
**diagnostic language** that explains warnings, degraded states, and ESTOP
causes — without changing their vehicle reaction.

The architecture is organized around **three independent artifacts**. None of
them owns the vehicle reaction.

```text
protocol/contracts/   = wire protocol only (3 DIAG_EVENT_RPT CAN messages)
protocol/diagnostics/  = diagnostic meaning (diagnostics.yaml registry)
DiagnosticManager      = runtime reporting only (no reaction, no new detection)
existing detector/control code = vehicle behavior (unchanged by Phase B)
```

## 1. Design principles

1. **Diagnostics never cause reactions.** `Detector → reaction + diag.raise(DiagId)`;
   never `diag → reaction`.
2. **Only `monitoring: IMPLEMENTED` detectors raise live events.**
3. **`UNOBSERVABLE` means genuinely not measurable**; `NOT_IMPLEMENTED` means
   measurable but no detector yet (not the same).
4. **Multiple diagnostics coexist per ECU** (an active set, not a singleton
   "current fault").
5. **`DiagId` describes cause**, not the reaction.
6. **`reaction` describes current implemented behavior**
   (`NONE/WARN/DERATE/INHIBIT/CSTOP/ESTOP`); `DSTOP` is removed from the
   vocabulary because it conflated *why* with *how*.
7. **Normal operating events are not diagnostics** (e.g. an ordinary obstacle
   stop is nominal motion control).
8. **Receiving `0x001` is safety propagation, not a fault.**
9. **Phase B does not silently add new safety behavior** — it instruments
   existing detectors only.
10. **Observability and monitoring are separate fields** (see §4).

## 2. Diagnostic identity

A diagnostic is identified by a globally-unique **`DiagId`** (16-bit). The wire
carries only the `DiagId` plus runtime state; all static meaning lives in the
registry and is resolved by the generated dictionary.

`DiagId` uses a **hybrid** encoding: the high byte is the reporting-ECU
namespace, the low byte is the event index.

```text
0x01xx = SYS
0x02xx = RT
0x03xx = MTR
```

Namespaces `0x04xx` and above are **reserved for future nodes**. Phase B defines
no PWT (or other) producer; reserving the range does not document a
non-existent Phase-B producer.

### Observation point, not physical root cause

A diagnostic records the **exact observation point and what was seen**, not an
invented physical root cause. For example `SYS_SEB_STATUS_TIMEOUT` means "SYS
stopped receiving valid SEB status" — the real physical cause (SEB power, wire,
connector, firmware, transceiver, bus-off) is *not* asserted unless evidence
distinguishes it. The registry hierarchy renders the observation point:

```text
SYS
└── source: SEB
    └── BRAKE
        └── SEB_STATUS
            └── TIMEOUT
```

## 3. Reaction vocabulary (behavioral)

Reactions describe *how the vehicle already behaves*, not why it stopped:

| Value | Meaning |
|---|---|
| `NONE` | reported only, no vehicle reaction |
| `WARN` | advisory / degraded, no control change |
| `DERATE` | reduce performance (reserved; no firmware path today) |
| `INHIBIT` | zero/disable the affected function (no coordinated stop) |
| `CSTOP` | controlled-stop behavior (coordinated deceleration/brake) |
| `ESTOP` | latched emergency stop via `0x001` / `0x011` |

`DSTOP`, `MSTOP`, `BSTOP`, `FSTOP` are deliberately **not** separate reaction
values. "Why it stopped" belongs to the `DiagId` cause; "how it stopped" is the
reaction. `CSTOP`/`DERATE` may have zero or few mappings today — that is fine;
the vocabulary is broader than current implementation.

`ESTOP` is a *safety state*, asserted by the existing `trigger_estop()` /
`force_estop()` paths and propagated by `0x001` / `0x011` (Phase A). The
diagnostic reports the cause; it does not create the state.

## 4. Diagnostic registry (`protocol/diagnostics/diagnostics.yaml`)

The registry describes **meaning, not CAN packing**. It is a separate file from
the CAN contracts so it does not pollute the meaning of `contracts/`.

Top level:

```yaml
schema_version: 1

namespaces:
  SYS: 0x01
  RT:  0x02
  MTR: 0x03

vocabularies:
  severity:       [INFO, WARNING, ERROR, CRITICAL]
  reaction:       [NONE, WARN, DERATE, INHIBIT, CSTOP, ESTOP]
  observability:  [DIRECT, DERIVED, COMMUNICATION, SELF_TEST, EXTERNAL_REPORTED, UNOBSERVABLE]
  monitoring:     [IMPLEMENTED, NOT_IMPLEMENTED]
  detection_basis: [DIRECT_HW, DIRECT_SW, DERIVED]
  failure_mode:   [ASSERTED, TIMEOUT, STALE, INVALID, IMPLAUSIBLE, MISMATCH,
                   FOLLOWING_ERROR, BUS_OFF, CRC_ERROR, COUNTER_ERROR,
                   WRITE_FAILED, INTERNAL_ERROR]
```

### `source` vs `evidence_sources`

```text
source
= the entity / function primarily being diagnosed

evidence_sources
= the nodes / signals used to reach the diagnosis
```

For example a brake/throttle conflict is diagnosed at the vehicle-control level
but corroborated by several ECUs:

```yaml
- id: 0x0124
  key: SYS_BRAKE_THROTTLE_CONFLICT
  reporter: SYS
  source: VEHICLE_CONTROL
  evidence_sources: [RT, MTR, SEB]
```

`source` is singular (the diagnosed entity); `evidence_sources` is an array of
the signals that support the conclusion.

### Example entry (with snapshot metadata)

```yaml
diagnostics:
  - id: 0x0102
    key: SYS_RT_HEARTBEAT_TIMEOUT
    reporter: SYS            # must equal id high byte (validated)
    source: RT               # entity/function primarily diagnosed
    evidence_sources: [RT]   # nodes/signals used to reach the diagnosis
    subsystem: COMMUNICATION
    component: RT_HEARTBEAT
    failure_mode: TIMEOUT
    severity: CRITICAL
    reaction: ESTOP          # present ONLY when monitoring: IMPLEMENTED
    observability: COMMUNICATION
    monitoring: IMPLEMENTED
    latching: true
    snapshot:                # decoded by generated Host dictionary; see §8.4
      encoding: U16
      quantity: elapsed_silence
      unit: ms
      scale: 1
      saturate: true
    evidence: "RT heartbeat freshness timeout"
    description: >
      SYS did not receive a fresh RT heartbeat within the existing
      firmware timeout.
```

### `monitoring: NOT_IMPLEMENTED` omits `reaction`

A `NOT_IMPLEMENTED` entry is a **coverage / future** diagnostic — no detector
exists, therefore no actual reaction occurs. Such entries **must not carry a
`reaction` field**. Planning intent may be recorded as `proposed_reaction`
(editorial only), but `proposed_reaction` is **excluded from generated firmware
metadata and from `DIAGNOSTICS_HASH`**. `reaction` always means the *current
implemented* vehicle behavior.

### `observability` vs `monitoring` (separate)

```text
hardware/signals could support detection
but firmware doesn't implement it
  → observability = DIRECT / DERIVED / ...
    monitoring    = NOT_IMPLEMENTED

hardware genuinely cannot know
  → observability = UNOBSERVABLE
    monitoring    = NOT_IMPLEMENTED   (coverage-map entry only)
```

"Observable but not monitored" and "genuinely unobservable" are distinct states
and must not be collapsed. Wheel-slip / differential-wheel-speed / actual-yaw
mismatch entries must be audited: if no independent measurement exists, they are
`UNOBSERVABLE`, not merely `NOT_IMPLEMENTED`.

### Traceability without rot

The canonical registry contains **no `file:`/`line:` references** (they rot
immediately). Implementation traceability uses a semantic field instead:

```yaml
evidence: "RT heartbeat freshness timeout"
```

Coverage documentation may contain current source references, but they do not
define the diagnostic identity.

### `latching`

`latching: true` marks an event that, once active, remains reported until an
explicit recovery/clear (e.g. ESTOP-class causes). `latching: false` marks
events that clear as soon as the condition disappears (e.g. a transient
timeout that recovers when communication resumes).

## 5. CAN wire contracts & diagnostic data payload

The three diagnostic report messages are ordinary CAN contracts. They use the
same DLC-8 structure and the **existing generator endianness** (big-endian, as
used by `sys_diag_rpt`); no new serialization convention is introduced.

```text
diag_id            u16   @ byte 0   (high byte = ECU namespace, low byte = event index)
state              u8    @ byte 2   (0 PENDING reserved, 1 ACTIVE, 2 LATCHED, 3 RECOVERED, 4 CLEARED, 5..255 reserved)
occurrence_count   u8    @ byte 3   (saturating 0..255; reset on clear()/boot)
report_counter     u8    @ byte 4   (modulo-256; reset on boot; one increment per transmitted frame incl. replay)
flags              u8    @ byte 5   (bit 0: FIRST_LOCAL_ESTOP_CAUSE, bit 1: SNAPSHOT_VALID, bits 2-7 reserved)
snapshot_data      u16   @ byte 6   (live contextual telemetry; decoded per-event from diagnostics.yaml, see §8.4)
```

### State enum

```text
0 PENDING        reserved — not emitted in Phase B (detector qualification is owned by the detector, not DiagnosticManager)
1 ACTIVE
2 LATCHED
3 RECOVERED
4 CLEARED
5..255           RESERVED
```

`DiagnosticManager` performs no debounce or detection, so it never produces
`PENDING`; the value is reserved for numeric compatibility only.

### Counters

- **`occurrence_count`** is a **saturating** counter (max 255) of distinct
  `raise()` qualifications since the last `clear()` or ECU boot. It is **not**
  incremented every control loop.
- **`report_counter`** is a **modulo-256** sequence counter, incremented once
  per transmitted `DIAG_EVENT_RPT` frame (including periodic replay). It
  **supports detection of dropped / duplicate / replayed frames** — it is not
  cryptographic replay protection.

### Diagnostic payload data fields

1. **`diag_id` (16-bit):** Identifies the exact failure mode and observation point.
2. **`state` (8-bit):** Lifecycle state (`ACTIVE` when tripped, `LATCHED` if
   requiring manual reset, `RECOVERED` when health returns, `CLEARED` after
   operator ack). `PENDING` is reserved and not emitted in Phase B.
3. **`occurrence_count` (8-bit):** Saturating count of distinct qualifications;
   helps surface intermittent issues (flaky connectors, intermittent CAN framing
   errors).
4. **`report_counter` (8-bit):** Supports detection of dropped/duplicate/replayed
   diagnostic frames (not cryptographic replay protection).
5. **`flags` (8-bit):**
   - `bit 0`: `FIRST_LOCAL_ESTOP_CAUSE` — set by the reporting ECU if this event
     was the first *local* ESTOP-class raise since boot. The manager makes **no
     global causality claim**; this is a local observation only.
   - `bit 1`: `SNAPSHOT_VALID` — indicates bytes 6-7 contain valid snapshot
     telemetry.
   - `bits 2..7`: reserved.
6. **`snapshot_data` (16-bit big-endian):** Decoded per-event from the registry
   `snapshot:` block (§8.4). Examples: timeout silence in ms; following/mismatch
   error (Δ speed mm/s, Δ angle 0.1°, Δ pressure kPa); `(TEC<<8)|REC`; packed
   offending-CAN-ID + DLC.

| Message | CAN ID | Instances |
|---|---|---|
| `SYS_DIAG_EVENT_RPT` | `0x601` | low `SYS→RT` (independent), high `RT→Host` (same_frame) |
| `RT_DIAG_EVENT_RPT` | `0x621` | high `RT→Host` (independent; RT transmits directly) |
| `MTR_DIAG_EVENT_RPT` | `0x631` | low `MTR→RT` (independent), high `RT→Host` (same_frame) |

### Routing

```text
LOW                                      HIGH

SYS  -- 0x601 --\
                     \
MTR  -- 0x631 -----> RT gateway ----------> Host

RT   --------------- 0x621 --------------> Host
```

`0x601` and `0x631` are added to the existing Low→High forwarding table and
forwarded **same-frame, unchanged** (no ID/state/counter rewrite; the counter
belongs to the original producer). `0x621` is not forwarded Low→High because RT
owns it directly on High. Diagnostic traffic uses the normal/best-effort TX
queue; `0x001` (ESTOP) keeps its high-priority path.

The CAN ID itself supplies the **reporter** (`0x601`→SYS, `0x621`→RT,
`0x631`→MTR), so `reporter` is not transmitted on the wire.

## 6. DiagnosticManager (runtime reporting)

A small per-ECU component, `shared/diagnostics.h`, owns runtime bookkeeping and
reporting. It does **not** dispatch reactions, debounce, or implement detection.

```cpp
void raise(DiagId id, uint16_t snapshot = 0);   // bookkeeping only
void recover(DiagId id);
void clear(DiagId id);
bool pop_pending_report(DiagReport& out);        // called by existing low-prio CAN TX task
```

It owns:

- the **active set** (many simultaneous events),
- per-event **state**,
- **occurrence_count** (incremented per qualification, not per loop),
- **report_counter** (serialization counter),
- **immediate report on state change**,
- **periodic active-set replay** (so a dropped frame is recovered),
- per-ECU **first_local_estop_cause** (local observation only — no global
  causality claim).

The **existing detector owns qualification**. Detector code that today reads:

```cpp
if (!heartbeat_ok()) {
    force_estop();
}
```

becomes, in Phase B:

```cpp
if (!heartbeat_ok()) {
    force_estop();                                   // existing reaction, unchanged
    diag.raise(DiagId::SysRtHeartbeatTimeout,
               static_cast<uint16_t>(silence_ms));   // bookkeeping only
}
// ... later, in the existing low-priority CAN TX task:
DiagReport r;
while (diag.pop_pending_report(r)) {
    can_tx_enqueue(encode_diag(r));                  // async, best-effort
}
```

### Firmware `DiagId` enum = IMPLEMENTED events only

The generated firmware `DiagId` enum contains **IMPLEMENTED events only**.
A `NOT_IMPLEMENTED` coverage ID is simply **unnameable from production
firmware** — this is stronger than a runtime `static_assert(diag_is_implemented(id))`,
which cannot work for an ordinary function argument. `diag.raise()` therefore
cannot reference a future/coverage-only ID at compile time.

### Hard implementation rules

> **Rule A — Control path first, diagnostic path cheap + non-blocking.**
> `raise()`, `recover()`, and `clear()` perform fixed-size in-memory bookkeeping
> only. They shall **not** allocate memory, block on locks/queues, transmit CAN
> synchronously, perform flash/NVS I/O, calculate expensive snapshots, or
> dispatch vehicle reactions.

> **Rule B — No detector from catalog.** Phase B shall not create a detector
> merely because a diagnostic exists in the coverage registry.
> `NOT_IMPLEMENTED` entries generate no production firmware instrumentation.

**Embedded manager internals** (no `std::map`, `std::vector`, `std::string`, or
`malloc`):

```cpp
struct DiagRuntime {
    bool     active;
    uint8_t  occurrence_count;   // saturating 0..255
    uint16_t snapshot;           // value supplied at raise() time
};
// fixed array / bitset indexed by the implemented-only DiagId enum
```

- `raise()` updates local state and marks a pending-report bit; it does **not**
  send CAN.
- The existing low-priority CAN TX task calls `pop_pending_report()` and enqueues
  to the normal/best-effort TX queue → `0x601/0x621/0x631`. ESTOP keeps its
  `0x001` priority path.
- **Snapshot rule:** use values already available at the detection point (e.g.
  `static_cast<uint16_t>(elapsed_ms)`). The manager never reads sensors/
  registers or queries other modules to compute a snapshot.
- **Per-ECU strictness:** RT strictest (O(1), no dynamic allocation, no
  synchronous CAN, no flash writes, no expensive snapshot work); SYS similar with
  slight tolerance for slower health diagnostics; MTR smallest fixed table, no
  extra complexity in the motor-actuation path.

### 6.1 Two Diagnostic Depths & Persistent Incident Memory

Production firmware maintains **two distinct diagnostic depths**:

```text
CAN Diagnostic Event Plane (0x601 / 0x621 / 0x631)
  └─ Actionable field information (DiagId + state + snapshot)
  └─ Real-time broadcast for Host, UI, and technicians

Local Post-Mortem Crash Data (ECU Internal Storage)
  └─ Deep post-mortem engineering data (ESP-IDF Core Dump / STM32 HardFault register dump)
  └─ Retrieved offline during lab failure analysis (PC, LR, CFSR, HFSR, stack backtrace)
```

#### Persistent Incident Memory (NVM)
To survive ECU resets (e.g. crash, brownout, watchdog reset), each ECU retains a small **8–16 record ring buffer** of critical incident records in NVM.

* **Strict Non-Blocking Write Rule:** `diag.raise()` updates RAM **only**. A low-priority background task asynchronously writes rate-limited incident records to NVM. Flash/NVM I/O is **never** invoked on the fault or control path.
* **Persisted Severity:** Persists only `ERROR`, `CRITICAL`, `ESTOP` causes, watchdog resets, brownouts, bus-offs, and critical actuator faults. Transient advisory warnings (`WARN`) are not written to NVM.

### 6.2 MCU Boot & Reset Cause Supervision

Every ECU exposes a **mandatory boot/reset cause diagnostic** after startup to immediately inform the network why the microcontroller restarted.

* **ESP32 (SYS / RT):** Query `esp_reset_reason()` on boot; report abnormal reset causes via `RT_ABNORMAL_RESET` / `SYS_ABNORMAL_RESET` (`TASK_WDT`, `INTERRUPT_WDT`, `PANIC_EXCEPTION`, `BROWNOUT`, `SOFTWARE_RESET`).
* **STM32 (MTR):** Query RCC reset flags (`RCC->CSR`) on boot before clearing; report `MTR_IWDG_RESET` or `MTR_HARDFAULT_RESET` (captured via Cortex-M `.noinit` fault handler).
* **Reset Classes:** `POWER_ON`, `SOFTWARE_RESET`, `WATCHDOG_RESET`, `BROWNOUT_RESET`, `PANIC_RESET`, `UNKNOWN`. Normal power-on is reported as nominal telemetry.

### 6.3 RAM Breadcrumb Ring Buffer (Pre-Crash Event Sequence)

Each ECU maintains a small, fixed **32-entry RAM ring buffer** (`struct Breadcrumb { uint32_t tick_ms; uint8_t event_code; uint16_t arg; }`) to record key state transitions immediately prior to a crash (e.g. `MODE_REQ_AUTO`, `STEER_SYNC_START`, `ESTOP_ASSERT`, `SAFETY_CLEAR_RX`).
* **Local Only:** Breadcrumbs do **not** generate CAN traffic.
* **Crash Survival:** Preserved across software restarts in `.noinit` RAM or embedded inside panic core dumps for post-mortem debugging.

### 6.4 Host-Side CAN Flight Recorder (Vehicle Event Timeline)

The Jetson Orin Host maintains a rolling **10–30 second in-memory buffer of all raw High CAN frames**.
* **Automatic Freeze:** Automatically freezes and writes a dated `.log` file on `0x001 ESTOP`, critical `DiagId`, ECU heartbeat loss, or manual debug trigger.
* **Time Window:** Preserves $10\text{ s}$ before + $5\text{ s}$ after the incident for chronological cross-ECU root-cause analysis.

### 6.5 Session & Build Identity Telemetry

To correlate field logs without polluting diagnostic payloads:
* **`boot_session_id`:** Random `uint32_t` generated at startup and sent in periodic health telemetry (`0x210`/`0x600`), allowing the Host to distinguish normal report counter wraps ($254 \rightarrow 255 \rightarrow 0$) from ECU reboots ($78 \rightarrow 0$).
* **Firmware Build Identity:** Git commit hash, `SEMANTIC_HASH`, `DIAGNOSTICS_HASH`, and `NETWORK_HASH` exposed in discovery telemetry.

## 7. Generated artifacts

The generator emits different amounts of metadata for firmware vs Host:

- **Compact C++** (`generated/cpp/diagnostics.hpp`) for ESP32/STM32 — `DiagId`
  enum (**IMPLEMENTED events only**) plus minimal runtime metadata
  (`latching`, snapshot encoding). No human-readable strings; severity/reaction/
  description stay Host-side. A `NOT_IMPLEMENTED` coverage ID does not exist in
  the firmware enum, so it is unnameable from production code:
  ```cpp
  enum class DiagId : uint16_t { SysEstopButtonAsserted = 0x0101, /* … implemented only … */ };
  struct DiagMetaLite { DiagId id; bool latching; /* snapshot encoding */ };
  constexpr const char* diag_key(DiagId);
  ```
- **Rich dictionary** (`generated/typescript/diagnostics.ts`,
  `generated/python/diagnostics.py`) for Host/UI — full reporter/source/
  evidence_sources/subsystem/component/failure_mode/severity/reaction/
  observability/monitoring/latching/snapshot/description.
- **Coverage markdown** (`generated/coverage/diagnostics.md`) — the coverage
  dictionary (IMPLEMENTED / NOT_IMPLEMENTED / UNOBSERVABLE), including
  code/documentation reaction mismatches.

A separate **`DIAGNOSTICS_HASH`** is added to `discovery.json` /
`capabilities.json`, hashed over the canonical diagnostic semantics
(`id/key/reporter/source/evidence_sources/subsystem/component/failure_mode/
severity/reaction/observability/monitoring/latching/snapshot`). `proposed_reaction`
and editorial `description` are excluded, so the hash is stable across
planning-only edits and two systems cannot agree on a `DiagId` but disagree on
bytes 6–7. This keeps `SEMANTIC_HASH` (CAN-message wire facts) and
`NETWORK_HASH` (topology/routing) stable when only a diagnostic is edited.

## 8. Verified event catalog

The following mapping was verified against the current firmware. `reaction`
records the **actual implemented behavior** and is shown **only for
`monitoring: IMPLEMENTED`** entries; `NOT_IMPLEMENTED` rows omit `reaction`
(planning intent lives in `proposed_reaction`, excluded from generated metadata
and `DIAGNOSTICS_HASH`). `NOT_IMPLEMENTED` entries are coverage/future software
diagnostics (no new hardware required) and are never raised live until their
detector is activated.

### 8.1 Active / Implemented Catalog

| DiagId | Key | reporter→source | reaction | monitoring | evidence (source) |
|---|---|---|---|---|---|
| `0x0101` | `SYS_ESTOP_BUTTON_ASSERTED` | SYS→SYS | ESTOP | IMPLEMENTED | `sys-esp32/src/main.cpp:488,497` |
| `0x0102` | `SYS_RT_HEARTBEAT_TIMEOUT` | SYS→RT | ESTOP | IMPLEMENTED | `main.cpp:497-498` |
| `0x0103` | `SYS_SEB_STATUS_TIMEOUT` | SYS→SEB | INHIBIT | IMPLEMENTED | `main.cpp:758-774` |
| `0x0104` | `SYS_SEB_L3_FAULT` | SYS→SEB | ESTOP | IMPLEMENTED | `main.cpp:418-448` |
| `0x0105` | `SYS_EGAS_L2_MISMATCH` | SYS→MTR | ESTOP | IMPLEMENTED | `main.cpp:516-546` |
| `0x0106` | `SYS_MTR_FBK_TIMEOUT` | SYS→MTR | INHIBIT | IMPLEMENTED | `main.cpp:566-578` |
| `0x0107` | `SYS_SEB_BRAKE_FOLLOW_ERR` | SYS→SEB | WARN | IMPLEMENTED | `main.cpp:376-402` |
| `0x0108` | `SYS_MTR_ESTOP_ACK_TIMEOUT` | SYS→MTR | ESTOP | IMPLEMENTED | `main.cpp:547-564` |
| `0x0109` | `SYS_CAN_BUS_OFF` | SYS→CAN | ESTOP | IMPLEMENTED | `main.cpp:958-967` |
| `0x010A` | `SYS_TASK_DEADLINE_MISSED` | SYS→SYS | WARN | IMPLEMENTED | `main.cpp:910-932` |
| `0x010B` | `SYS_GEAR_MISMATCH` | SYS→MTR | WARN | IMPLEMENTED | `main.cpp:672-679` |
| `0x010C` | `SYS_SEB_TEMP_HIGH` | SYS→SEB | WARN | IMPLEMENTED | `main.cpp:406-416` |
| `0x010D` | `SYS_CAN_RX_OVERFLOW` | SYS→CAN | WARN | IMPLEMENTED | `main.cpp:199-203` |
| `0x010E` | `SYS_RT_SETPOINT_STALE` | SYS→RT | INHIBIT | IMPLEMENTED | `main.cpp:701-705,735` |
| `0x0201` | `RT_HOST_HEARTBEAT_TIMEOUT` | RT→Host | ESTOP ⚠️ | IMPLEMENTED | `rt-esp32/src/safety_monitor.h:116-124` |
| `0x0202` | `RT_SYS_HEARTBEAT_TIMEOUT` | RT→SYS | ESTOP | IMPLEMENTED | `safety_monitor.h:98-114` |
| `0x0203` | `RT_STEER_FOLLOWING_ERROR` | RT→SES | ESTOP | IMPLEMENTED | `safety_monitor.h:126-150` |
| `0x0204` | `RT_CAN_BUS_OFF` | RT→CAN | ESTOP | IMPLEMENTED | `rt-esp32/src/can_health.h:53-94` |
| `0x0205` | `RT_CAN_HIGH_BUS_OFF` | RT→CAN | ESTOP | IMPLEMENTED | `rt-esp32/src/can_health.h:79-94` |
| `0x0206` | `RT_SES_L3_FAULT` | RT→SES | ESTOP | IMPLEMENTED | `rt-esp32/src/can_dispatch.h:243-255` |
| `0x0207` | `RT_SEB_L3_FAULT` | RT→SEB | ESTOP | IMPLEMENTED | `rt-esp32/src/can_dispatch.h:297-309` |
| `0x0208` | `RT_HOST_DRIVE_CMD_STALE` | RT→Host | INHIBIT | IMPLEMENTED | `rt-esp32/src/main.cpp:749-759` |
| `0x0209` | `RT_STEER_SYNC_TIMEOUT` | RT→SES | INHIBIT | IMPLEMENTED | `rt-esp32/src/steering_control.h:68-71` |
| `0x020A` | `RT_STEER_IMPLAUSIBLE_ANGLE` | RT→SES | INHIBIT | IMPLEMENTED | `rt-esp32/src/steering_control.h:78-82` |
| `0x020B` | `RT_STEER_ESTOP_JAM` | RT→SES | INHIBIT | IMPLEMENTED | `rt-esp32/src/steering_control.h:120-126` |
| `0x020C` | `RT_SYS_SAFETY_STS_LOSS` | RT→SYS | ESTOP | IMPLEMENTED | `rt-esp32/src/main.cpp:346-350` |
| `0x020D` | `RT_LOW_CAN_PEER_TIMEOUT` | RT→CAN | INHIBIT | IMPLEMENTED | `rt-esp32/src/main.cpp:267-270` |
| `0x020F` | `RT_TASK_HEALTH_FAULT` | RT→RT | WARN | IMPLEMENTED | `rt-esp32/src/main.cpp:764-775` |
| `0x0210` | `RT_SES_TELEMETRY_WARN` | RT→SES | WARN | IMPLEMENTED | `rt-esp32/src/can_dispatch.h:282-284` |
| `0x0211` | `RT_CAN_HIGH_RX_OVERFLOW` | RT→CAN | WARN | IMPLEMENTED | `rt-esp32/src/main.cpp:148` |
| `0x0212` | `RT_DIRECT_STEER_STALE` | RT→Host | WARN | IMPLEMENTED | `rt-esp32/src/phase2_motion.h:16-31` |
| `0x0213` | `RT_MTR_FBK_TIMEOUT` | RT→MTR | INHIBIT | IMPLEMENTED | `rt-esp32/src/main.cpp:78, phase2_motion.h:54` |
| `0x0301` | `MTR_RT_DRIVE_CMD_TIMEOUT` | MTR→RT | INHIBIT | IMPLEMENTED | `mtr-stm32/src/motor_manager.h:329-333` |
| `0x0305` | `MTR_SYS_SAFETY_STS_TIMEOUT` | MTR→SYS | INHIBIT | IMPLEMENTED | `mtr-stm32/src/can_handler.cpp` |
| `0x0306` | `MTR_SYS_SAFETY_CRC_ERROR` | MTR→SYS | WARN | IMPLEMENTED | `mtr-stm32/src/can_handler.cpp` |
| `0x0307` | `MTR_SYS_SAFETY_COUNTER_STALE`| MTR→SYS | INHIBIT | IMPLEMENTED | `mtr-stm32/src/can_handler.cpp` |
| `0x0308` | `MTR_SYS_MODE_CMD_TIMEOUT` | MTR→SYS | INHIBIT | IMPLEMENTED | `mtr-stm32/src/can_handler.cpp` |
| `0x0309` | `MTR_SYS_PWR_CMD_TIMEOUT` | MTR→SYS | INHIBIT | IMPLEMENTED | `mtr-stm32/src/can_handler.cpp` |
| `0x030A` | `MTR_REARM_SEQUENCE_VIOLATION`| MTR→SYS | INHIBIT | IMPLEMENTED | `mtr-stm32/src/motor_manager.h` |
| `0x030D` | `MTR_FDCAN_BUS_OFF` | MTR→CAN | INHIBIT | IMPLEMENTED | `mtr-stm32/src/can_handler.cpp` |
| `0x030E` | `MTR_FDCAN_RX_OVERFLOW` | MTR→CAN | WARN | IMPLEMENTED | `mtr-stm32/src/can_handler.cpp` |
| `0x030F` | `MTR_SPEED_SETPOINT_INVALID` | MTR→RT | WARN | IMPLEMENTED | `mtr-stm32/src/motor_manager.h` |
| `0x0311` | `MTR_CMD_STREAM_UNAUTHORISED`| MTR→RT | INHIBIT | IMPLEMENTED | `mtr-stm32/src/motor_manager.h` |

---

### 8.2 Pure Software Diagnostic Capabilities (Zero Hardware Additions)

The following diagnostics can be evaluated and emitted purely through software
logic, using existing CAN bus traffic, existing GPIOs, FreeRTOS metrics, and
internal on-chip peripherals. No new wiring or sensors are required. `reaction`
is omitted for all `NOT_IMPLEMENTED` entries below.

#### 8.2.1 SYS Node Capabilities (`0x01xx`)

| DiagId | Key | reporter→source | reaction | monitoring | Diagnostic Mechanism / Source Data |
|---|---|---|---|---|---|
| `0x0120` | `SYS_RT_COUNTER_FROZEN` | SYS→RT | — | NOT_IMPLEMENTED | Detects `0x7FD` frames arriving with deadlocked/repeating `alive_ctr` |
| `0x0121` | `SYS_RT_DEGRADED_STATE` | SYS→RT | — | NOT_IMPLEMENTED | Decodes `0x210` byte 1 (`safety_state > 0` or nonzero `estop_reason`) |
| `0x0122` | `SYS_RT_TASK_UNHEALTHY` | SYS→RT | — | NOT_IMPLEMENTED | Decodes `0x210` byte 4 (`task_health != 0xFF`) indicating RT task starvation |
| `0x0123` | `SYS_RT_STEER_FAULT_RPT` | SYS→RT | — | NOT_IMPLEMENTED | Decodes `0x210` byte 5 (`steer_state` indicating degraded/failed steering) |
| `0x0124` | `SYS_BRAKE_THROTTLE_CONFLICT`| SYS→VEHICLE_CONTROL | — | NOT_IMPLEMENTED | Correlates SEB brake pressure $>100$ kPa with commanded speed $>0$ mm/s; requires careful state invariant checking to avoid false positives during deceleration |
| `0x0125` | `SYS_MTR_UNCOMMANDED_PROPULSION`| SYS→MTR | — | NOT_IMPLEMENTED | EGAS L2: MTR reports speed $>0$ while mode is MANUAL/OFF or power is OFF (verify non-overlap with EGAS mismatch) |
| `0x0126` | `SYS_SAFETY_STREAM_CRC_ERR` | SYS→SYS | — | NOT_IMPLEMENTED | Internal E2E CRC-8 generation verification mismatch on `0x011` payload |
| `0x0130` | `SYS_SEB_ROLLING_FROZEN` | SYS→SEB | — | NOT_IMPLEMENTED | Detects `0x721` rolling counter unchanged for $>100$ ms (SEB CPU deadlock) |
| `0x0131` | `SYS_SEB_NOT_ALIGNED` | SYS→SEB | — | NOT_IMPLEMENTED | Inspects `0x721` byte 0 bit 0 (`alignment_status == 0`) after boot grace |
| `0x0132` | `SYS_SEB_PRESSURE_BUILD_FAILURE` | SYS→SEB | — | NOT_IMPLEMENTED | Full stroke request (`0x7B9`) + near-zero motor current (`0x6FB`) + low pressure. Canonical monitor choice (SYS vs RT). Possible causes: line rupture, air, empty reservoir |
| `0x0133` | `SYS_SEB_CALIPER_BIND` | SYS→SEB | — | NOT_IMPLEMENTED | Excessive motor current (`0x6FB`) observed at low stroke setpoints |
| `0x0134` | `SYS_SEB_TEMP_RATE_HIGH` | SYS→SEB | — | NOT_IMPLEMENTED | High $\Delta T / \Delta t$ calculated from `0x6FB` ECU temperature telemetry |
| `0x0135` | `SYS_SEB_CHECKSUM_ERROR` | SYS→SEB | — | NOT_IMPLEMENTED | XOR8-complement checksum mismatch on incoming `0x721` / `0x731` |
| `0x0136` | `SYS_SEB_UNCOMMANDED_BRAKING` | SYS→SEB | — | NOT_IMPLEMENTED | Ghost braking: `0x721` reports pressure $>500$ kPa while no brake commanded |
| `0x0140` | `SYS_MTR_ROLLAWAY` | SYS→MTR | — | NOT_IMPLEMENTED | Vehicle speed $> 100$ mm/s on `0x206` while in Neutral without throttle |
| `0x0141` | `SYS_MTR_STALL` | SYS→MTR | — | NOT_IMPLEMENTED | High commanded speed on `0x204` for $> 1.0$ s with zero measured speed on `0x206` |
| `0x0142` | `SYS_MTR_PARTIAL_CRASH` | SYS→MTR | — | NOT_IMPLEMENTED | STM32 broadcasting `0x120` (`SYS_THROTTLE_STS`) but failing to emit `0x206` |
| `0x0150` | `SYS_SES_STATUS_TIMEOUT` | SYS→SES | — | NOT_IMPLEMENTED | Low CAN monitor: complete absence of EPS-C `0x201` during vehicle motion |
| `0x0151` | `SYS_SES_L3_FAULT` | SYS→SES | — | NOT_IMPLEMENTED | Low CAN monitor: decodes critical steer faults directly from EPS-C `0x202` |
| `0x0160` | `SYS_LEVER_SWITCH_HELD_ACTIVE_LONG`| SYS→SYS | — | NOT_IMPLEMENTED | Physical brake lever GPIO 2 held active continuously for $>60$ s while speed $>0$ (rider holding brake vs stuck switch) |
| `0x0161` | `SYS_START_BUTTON_STUCK_ACTIVE` | SYS→SYS | — | NOT_IMPLEMENTED | Momentary START (GPIO 41) held LOW $>10$ s |
| `0x0162` | `SYS_MODE_BUTTON_STUCK_ACTIVE` | SYS→SYS | — | NOT_IMPLEMENTED | MODE (GPIO 11) button held LOW $>10$ s |
| `0x0163` | `SYS_SWITCH_CONFLICT` | SYS→SYS | — | NOT_IMPLEMENTED | Handlebar Left (GPIO 9) and Right (GPIO 6) turn inputs active simultaneously |
| `0x0170` | `SYS_CAN_BUS_PASSIVE` | SYS→CAN | — | NOT_IMPLEMENTED | TWAI controller enters Error-Passive state (TEC or REC $> 127$) |
| `0x0171` | `SYS_CAN_SIGNAL_DEGRADED` | SYS→CAN | — | NOT_IMPLEMENTED | Spiking REC with valid frame decode errors (loose wire / termination noise) |
| `0x0172` | `SYS_CAN_BABBLING_NODE` | SYS→CAN | — | DEFERRED | Simple per-ID timestamp check (`last_rx_tick`). Low priority unless bus abuse occurs |
| `0x0173` | `SYS_CAN_INVALID_DLC` | SYS→CAN | — | NOT_IMPLEMENTED | Received frame DLC mismatches contract (e.g. `0x001` DLC $> 0$ or `0x204` DLC $\ne 5$) |
| `0x0174` | `SYS_CAN_ESTOP_FLOOD` | SYS→CAN | — | NOT_IMPLEMENTED | Rate-limit violation on incoming `0x001` ($>2$ frames / 500 ms) |
| `0x0175` | `SYS_DUAL_SENDER_CONFLICT` | SYS→RT | — | NOT_IMPLEMENTED | Dual sender collision: both RT and SYS transmitting `0x7B9` on low bus |
| `0x0180` | `SYS_SOC_TEMP_HIGH` | SYS→SYS | — | NOT_IMPLEMENTED | On-die ESP32-S3 silicon temperature sensor exceeds safe thermal threshold |
| `0x0181` | `SYS_STACK_HIGH_WATER` | SYS→SYS | — | DEFERRED | FreeRTOS task stack high-water mark approaches exhaustion ($< 256$ bytes) |
| `0x0182` | `SYS_BROWNOUT_DETECTED` | SYS→SYS | — | NOT_IMPLEMENTED | NVS reset reason registers brownout (`ESP_RST_BROWNOUT`); evidence of MCU brownout reset |
| `0x0183` | `SYS_MODE_SPLIT_BRAIN` | SYS→RT | — | NOT_IMPLEMENTED | Qualified mode desync: SYS mode `0x110` vs RT mode `0x210` desync after completed handshake timeout ($>1.0$ s) |
| `0x0184` | `SYS_HMI_MODE_REQ_TIMEOUT` | SYS→HMI | — | NOT_IMPLEMENTED | HMI mode request `0x111` or power request `0x112` stream stale / frozen counter |
| `0x0185` | `SYS_HEAP_LOW_WARNING` | SYS→SYS | — | NOT_IMPLEMENTED | Boot/slow periodic health check of available heap if 3rd-party libs allocate at runtime |
| `0x0186` | `SYS_CPU_CORE_SATURATED` | SYS→SYS | — | OMITTED | Omitted: task deadline/watchdog monitoring gives more actionable evidence with zero complexity |
| `0x0187` | `SYS_MUTEX_DEADLOCK_TRIP` | SYS→SYS | — | NOT_IMPLEMENTED | Mutex acquire timeout ($>500$ ms) on critical CAN driver or peripheral lock |
| `0x0188` | `SYS_WATCHDOG_PET_FAILURE` | SYS→SYS | — | NOT_IMPLEMENTED | Internal FreeRTOS Task Watchdog Timer (TWDT) flags starvation on registered task |
| `0x0189` | `SYS_NVS_STORAGE_CORRUPT` | SYS→SYS | — | NOT_IMPLEMENTED | NVS flash partition CRC validation failure or wear-out write abort |

#### 8.2.2 RT Node Capabilities (`0x02xx`)

| DiagId | Key | reporter→source | reaction | monitoring | Diagnostic Mechanism / Source Data |
|---|---|---|---|---|---|
| `0x0220` | `RT_SYS_HB_ECHO_LOST` | RT→SYS | — | NOT_IMPLEMENTED | `0x011` byte 1 reads `SYS_HeartbeatOk == 0` while RT is actively transmitting `0x7FD` (proves RT low-bus TX line/transceiver severed) |
| `0x0221` | `RT_SEB_STATUS_TIMEOUT` | RT→SEB | — | NOT_IMPLEMENTED | SEB `0x721` stops arriving for $>50$ ms during ACTIVE mode (mid-drive brake actuator loss) |
| `0x0222` | `RT_SEB_PRESSURE_BUILD_FAILURE` | RT→SEB | — | NOT_IMPLEMENTED | `0x721` reports stroke near maximum ($>20$ mm) while line pressure remains $<500$ kPa. Possible causes: line rupture, fluid leak, air |
| `0x0223` | `RT_SEB_CALIPER_DRAG` | RT→SEB | — | NOT_IMPLEMENTED | Zero brake commanded (`0x7B9`) but `0x721` line pressure persists $>500$ kPa or stroke $>5$ mm (caliper stick) |
| `0x0224` | `RT_STEER_HIGH_OPPOSING_LOAD` | RT→SES | — | NOT_IMPLEMENTED | Measured steering torque (`0x201`) or motor current (`0x6FA` $>20$ A) excessive for requested slew rate (indicates high load; binding, curb impact, or tire/road load) |
| `0x0225` | `RT_STEER_OPPOSING_TORQUE_SPIKE` | RT→SES | — | NOT_IMPLEMENTED | Sharp measured torque spike opposing commanded direction (front wheel struck obstacle/curb) |
| `0x0226` | `RT_MTR_NO_MOTION_UNDER_COMMAND` | RT→MTR | — | NOT_IMPLEMENTED | Commanded speed $>0$ on `0x204` but measured speed 0 on `0x206` for $>1.0$ s with zero brake |
| `0x0227` | `RT_MTR_RUNAWAY_MISMATCH` | RT→MTR | — | NOT_IMPLEMENTED | Measured speed on `0x206` $>500$ mm/s while RT setpoint is 0 and gear is Neutral |
| `0x0228` | `RT_THROTTLE_BRAKE_CONFLICT`| RT→MTR | — | NOT_IMPLEMENTED | Motor speed $>0$ on `0x206` while active hydraulic brake pressure $>1000$ kPa on `0x721` |
| `0x0229` | `RT_PLANNER_CONTRADICTION` | RT→Host | — | NOT_IMPLEMENTED | Host sends conflicting commands simultaneously: drive $>1000$ mm/s (`0x300`) and brake $>2000$ kPa (`0x301`) |
| `0x022A` | `RT_PLANNER_OBSTACLE_CONFLICT`| RT→Host | — | NOT_IMPLEMENTED | Host requests forward drive speed $>1000$ mm/s while obstacle distance $\le 300$ mm on `0x400` |
| `0x022B` | `RT_TRANSMISSION_SHIFT_ILLEGAL`| RT→Host | — | NOT_IMPLEMENTED | Reverse gear ($R$) commanded while vehicle forward velocity $>500$ mm/s |
| `0x022C` | `RT_HMI_SYS_MODE_DEADLOCK` | RT→SYS | — | NOT_IMPLEMENTED | HMI requested AUTO (`0x111`) but SYS fails to transition `SYS_Mode` (`0x110`) within 3 s without ESTOP |
| `0x022D` | `RT_SUPPLY_VOLTAGE_SAG` | RT→SES | — | NOT_IMPLEMENTED | EPS-C `0x6FA` supply voltage drops below $11.0$ V under actuator load |
| `0x022E` | `RT_CAN_BUS_FLOOD_DETECTED` | RT→CAN | — | DEFERRED | Inter-arrival time of any single CAN ID $<2$ ms ($>500$ Hz babbling node / DoS condition) |
| `0x022F` | `RT_CAN_JITTER_EXCESSIVE` | RT→CAN | — | DEFERRED | Periodic frame arrival jitter exceeds $\pm 10$ ms (CAN bus arbitration starvation) |
| `0x0230` | `RT_SOC_TEMP_HIGH` | RT→RT | — | NOT_IMPLEMENTED | On-chip ESP32-S3 silicon temperature sensor exceeds $90^\circ$C |
| `0x0231` | `RT_BROWNOUT_DETECTED` | RT→RT | — | NOT_IMPLEMENTED | ESP32-S3 internal 3.3V brownout interrupt triggered ($V_{\text{DD33}} < 2.8$ V); evidence of MCU brownout reset |
| `0x0232` | `RT_STACK_HIGH_WATER` | RT→RT | — | DEFERRED | FreeRTOS task stack margin approaches exhaustion ($< 256$ bytes) |
| `0x0233` | `RT_ROLLOVER_RISK_HIGH` | RT→RT | — | NOT_IMPLEMENTED | Calculated lateral acceleration $a_y \approx \frac{v^2 \tan\delta}{L}$ exceeds dynamic stability threshold |
| `0x0234` | `RT_MOTOR_STALL_DETECTED` | RT→MTR | — | NOT_IMPLEMENTED | Commanded speed $>0$ mm/s on `0x204` for $>1.0$ s with zero encoder pulse delta and zero brake |
| `0x0235` | `RT_UNCOMMANDED_ROLLAWAY` | RT→MTR | — | NOT_IMPLEMENTED | Rear encoder detects vehicle speed $>100$ mm/s while parked or in Neutral without throttle |
| `0x0236` | `RT_DIRECTION_ROLLBACK_CONFLICT`| RT→MTR | — | NOT_IMPLEMENTED | Commanded Drive ($D$) but encoder measures reverse motion, or commanded Reverse ($R$) but rolling forward |
| `0x0237` | `RT_DYNAMIC_CLAMP_EXCEEDED` | RT→Host | — | NOT_IMPLEMENTED | Commanded steering angle exceeds vehicle speed-dependent stability limit $\delta_{\text{max}}(v)$ |
| `0x0238` | `RT_CAN_HIGH_TX_FAILURE`| RT→CAN | — | NOT_IMPLEMENTED | Evidence-only: MCP2515 SPI responsive but CAN TX failing (causes: transceiver, wiring, termination, missing ACK, stuck dominant) |
| `0x0239` | `RT_SPEED_TRACKING_ERROR` | RT→MTR | — | NOT_IMPLEMENTED | Closed-loop following error: $|v_{\text{target}} - v_{\text{encoder}}| > 500$ mm/s for $>1$ s |
| `0x023A` | `RT_HOST_OBSTACLE_DIST_TIMEOUT`| RT→Host | — | NOT_IMPLEMENTED | Host `0x400` obstacle distance frame timed out during autonomous travel |
| `0x023B` | `RT_MTR_ENCODER_SPEED_MISMATCH`| RT→MTR | — | NOT_IMPLEMENTED | Measured speed on `0x206` diverges from PCNT rear encoder pulses $>200$ mm/s (encoder slip / pulse noise) |
| `0x023C` | `RT_WHEEL_SLIP_DETECTED` | RT→MTR | — | UNOBSERVABLE | Unobservable without independent 4-wheel speed sensors; omitted from Phase B/C |
| `0x023D` | `RT_DIFF_WHEEL_SPEED_MISMATCH`| RT→MTR | — | UNOBSERVABLE | Unobservable without independent rear wheel speed sensors; omitted from Phase B/C |
| `0x023E` | `RT_YAW_RATE_KINEMATIC_MISMATCH`| RT→Host | — | UNOBSERVABLE | Unobservable without independent calibrated IMU/yaw rate sensor; omitted from Phase B/C |
| `0x023F` | `RT_SPIN_IN_PLACE_REJECTED` | RT→Host | — | NOT_IMPLEMENTED | Host requested $v=0, \omega > 0$ which is kinematically impossible on a tricycle |
| `0x0240` | `RT_STEER_SLEW_RATE_EXCEEDED` | RT→SES | — | NOT_IMPLEMENTED | Commanded or measured steering angle rate of change exceeds $525^\circ$/s |
| `0x0241` | `RT_STEER_ANGLE_OFFSET_DRIFT` | RT→SES | — | NOT_IMPLEMENTED | Steer center finding alignment departs $>5.0^\circ$ from calibrated straight-ahead neutral |
| `0x0242` | `RT_SES_CONTROL_ENABLE_REJECTED`| RT→SES | — | NOT_IMPLEMENTED | EPS-C fails to transition to active control mode feedback within 200 ms of command |
| `0x0243` | `RT_SEB_PRESSURE_RESPONSE_SLUGGISH`| RT→SEB | — | NOT_IMPLEMENTED | Hydraulic line pressure rise time $>200$ ms behind commanded step (fluid air bubbles) |
| `0x0244` | `RT_SEB_PAD_WEAR_LIMIT` | RT→SEB | — | NOT_IMPLEMENTED | Stroke required to achieve 1000 kPa exceeds 22 mm (approaching physical travel limit) |
| `0x0245` | `RT_SEB_CONTROL_ENABLE_REJECTED`| RT→SEB | — | NOT_IMPLEMENTED | SEB fails to transition to active control enable feedback within 200 ms of command |
| `0x0246` | `RT_SEB_SUBZERO_TEMP_WARN` | RT→SEB | — | NOT_IMPLEMENTED | SEB ECU/fluid temperature $<-10^\circ$C risking high fluid viscosity / sluggish brake actuation |
| `0x0247` | `RT_MCP2515_SPI_COMM_FAIL` | RT→CAN | — | NOT_IMPLEMENTED | High CAN MCP2515 SPI bus failure: MISO/MOSI/SCK/CS line severed, transaction timeout, or register echo failure |
| `0x0248` | `RT_CAN_LOW_BUS_PASSIVE` | RT→CAN | — | NOT_IMPLEMENTED | TWAI controller enters Error-Passive state (TEC or REC $>127$) |
| `0x0249` | `RT_CAN_HIGH_BUS_PASSIVE` | RT→CAN | — | NOT_IMPLEMENTED | MCP2515 controller enters Error-Passive state (TEC or REC $>127$) |
| `0x024A` | `RT_CAN_INVALID_DLC` | RT→CAN | — | NOT_IMPLEMENTED | Frame received with unexpected DLC (e.g. `0x001` with DLC $>0$ or `0x300` with DLC $\ne 8$) |
| `0x024B` | `RT_GATEWAY_DROP_OVERFLOW` | RT→CAN | — | NOT_IMPLEMENTED | High-to-Low or Low-to-High gateway queue saturated, dropping forwarded frames |
| `0x024C` | `RT_HEAP_LOW_WARNING` | RT→RT | — | NOT_IMPLEMENTED | Boot/slow periodic health check of available heap if 3rd-party libs allocate at runtime |
| `0x024D` | `RT_KINEMATIC_SOLVER_SINGULARITY`| RT→RT | — | BACKLOG | Control output NaN/Inf guard (`std::isfinite()`); cheap Phase-C backlog guard |
| `0x024E` | `RT_NVS_STORAGE_FAULT` | RT→RT | — | NOT_IMPLEMENTED | Non-volatile storage write/read failure on calibration or runtime persistence |
| `0x024F` | `RT_SYS_SAFETY_COUNTER_STALE` | RT→SYS | — | NOT_IMPLEMENTED | Rolling counter in `0x011` stopped advancing |
| `0x0250` | `RT_SES_STATUS_COUNTER_STALE` | RT→SES | — | NOT_IMPLEMENTED | Rolling counter in `0x201` (EPS-C status) stopped advancing |
| `0x0251` | `RT_SEB_STATUS_COUNTER_STALE` | RT→SEB | — | NOT_IMPLEMENTED | Rolling counter in `0x721` (SEB status) stopped advancing |
| `0x0252` | `RT_HOST_DRIVE_COUNTER_STALE` | RT→Host | — | NOT_IMPLEMENTED | Rolling counter in `0x300` (host drive cmd) stopped advancing |
| `0x0253` | `RT_ENCODER_GLITCH_OVERFREQ` | RT→RT | — | NOT_IMPLEMENTED | GPIO 1/2 PCNT pulse frequency exceeds physical maximum ($>50$ km/h) |
| `0x0254` | `RT_HOST_CLOCK_SKEW_EXCESSIVE` | RT→Host | — | NOT_IMPLEMENTED | Host command message timestamp / sequence clock drifts $>200$ ms relative to RT tick |
| `0x0255` | `RT_SEB_PRESSURE_ZERO_DRIFT` | RT→SEB | — | NOT_IMPLEMENTED | Rest transducer pressure $>150$ kPa when brake pushrod is confirmed fully retracted ($0$ mm) |
| `0x0256` | `RT_MTR_TEMPERATURE_HIGH` | RT→MTR | — | NOT_IMPLEMENTED | Motor controller or winding temperature on `0x206`/`0x600` exceeds thermal derating limit |
| `0x0257` | `RT_TASK_DEADLINE_MISSED` | RT→RT | — | NOT_IMPLEMENTED | Control loop (100 Hz) execution time or scheduling period exceeds threshold ($>15$ ms) |
| `0x0258` | `RT_HEAP_FRAGMENTATION_HIGH` | RT→RT | — | OMITTED | Omitted under zero-runtime-allocation policy |
| `0x0259` | `RT_SAFETY_QUEUE_OVERFLOW` | RT→RT | — | NOT_IMPLEMENTED | FreeRTOS safety event queue `g_safety_evt_q` saturated; dropped transition event |
| `0x025A` | `RT_PID_INTEGRATOR_SATURATED` | RT→RT | — | NOT_IMPLEMENTED | Speed PID integral error accumulator clamped at ceiling/floor for $>2.0$ s |
| `0x025B` | `RT_LOCAL_ESTOP_LATCH_PREVENT_CLEAR` | RT→RT | — | NOT_IMPLEMENTED | SYS issued `SAFETY_CLEAR` but local RT latch (following error / obstacle) actively blocks release |
| `0x025C` | `RT_SYS_SAFETY_CRC_ERROR` | RT→SYS | — | NOT_IMPLEMENTED | E2E CRC-8 Autosar mismatch on received `0x011` safety status frame |
| `0x025D` | `RT_CALIBRATION_CORRUPTED` | RT→RT | — | NOT_IMPLEMENTED | Stored calibration parameters in NVS fail CRC32 verification on startup |

#### 8.2.3 MTR Node Capabilities (`0x03xx`)

| DiagId | Key | reporter→source | reaction | monitoring | Diagnostic Mechanism / Source Data |
|---|---|---|---|---|---|
| `0x0302` | `MTR_THROTTLE_IMPLAUSIBLE` | MTR→MTR | — | NOT_IMPLEMENTED | Calculated DAC code outside clamped range $[655, 1966]$ |
| `0x0303` | `MTR_ADC_FAULT` | MTR→MTR | — | NOT_IMPLEMENTED | Throttle grip ADC stuck-at-rail (0 or 4095) in manual mode |
| `0x0304` | `MTR_GEAR_CONFLICT` | MTR→MTR | — | NOT_IMPLEMENTED | Drive (PA2) and Reverse (PA0) relays commanded active simultaneously |
| `0x030C` | `MTR_I2C_DAC_NACK` | MTR→DAC | — | NOT_IMPLEMENTED | MCP4725 DAC fails to acknowledge address $0x60/0x61/0x62$ on PA5/PA7 |
| `0x0310` | `MTR_I2C_BUS_LOCKUP` | MTR→DAC | — | NOT_IMPLEMENTED | I2C SDA line held low by peripheral (slave bus lockup condition) |
| `0x0312` | `MTR_SYS_HEARTBEAT_LOSS` | MTR→SYS | — | NOT_IMPLEMENTED | SYS heartbeat `0x7FE` absence $>500$ ms |
| `0x0313` | `MTR_CLOCK_CALIBRATION_DRIFT` | MTR→MTR | — | OMITTED | Omitted: arbitration, queue, and ISR latency contaminate CAN arrival timing |

---


---

### 8.3 Complete Diagnostic Data Transmitted by Channel & Node

All three controller nodes broadcast both **periodic health state** and **event-driven diagnostic records** across the CAN buses:

#### 8.3.1 SYS Node Channels
1. **Periodic Health Telemetry (`0x600 SYS_DIAG_RPT` @ 1 Hz):**
   * Vehicle Mode (`mode`): `MANUAL=0, AUTO=1, ESTOP=2`
   * Brake Lever State (`brake_engaged`): physical microswitch on GPIO 2
   * Brake System Fault (`brake_fault`): L3 fault reported by SEB
   * Peer RT Heartbeat Freshness (`heartbeat_ok`): 1 if fresh, 0 if timed out
   * Latched ESTOP Status (`estop_active`): 1 if vehicle is in emergency stop
   * Free Heap Memory (`free_heap_kb`): uint16 RAM availability
   * CAN Electrical Counters: `tec` (Transmit Error Counter), `rec` (Receive Error Counter)
   * Buffer Health (`rx_overflow`): TWAI FIFO overflow count
   * FreeRTOS Task Bitmask (8 tasks: safety, brake, dispatch, can_tx, can_ctrl, hb, mode, gear)

2. **Event-Driven Fault Reports (`0x601 SYS_DIAG_EVENT_RPT`):**
   * `diag_id` (u16): Event identifier from Table 8.1 & 8.2.1
   * `state` (u8): `PENDING=0 (reserved)`, `ACTIVE=1`, `LATCHED=2`, `RECOVERED=3`, `CLEARED=4`
   * `occurrence_count` (u8): Saturating qualification counter (0..255)
   * `report_counter` (u8): Modulo-256 sequence number
   * `flags` (u8): `FIRST_LOCAL_ESTOP_CAUSE` (`bit 0`), `SNAPSHOT_VALID` (`bit 1`)
   * `snapshot_data` (u16): Context telemetry (decoded per-event, see §8.4)

3. **Safety Status Broadcast (`0x011 SYS_SAFETY_STS` @ 5 Hz / 200 ms):**
   * Latched E-stop flag (`estop_active`), Heartbeat OK flag, Lighting indicators, 8-bit rolling counter, E2E CRC-8

4. **Heartbeat (`0x7FE SYS_HEARTBEAT` @ 10 Hz / 100 ms):**
   * 8-bit alive counter, module health flags

#### 8.3.2 RT Node Channels
1. **Event-Driven Fault Reports (`0x621 RT_DIAG_EVENT_RPT` on High CAN):**
   * `diag_id` (u16): Event identifier (`0x02xx` from Table 8.1 & 8.2.2)
   * `state` (u8): `PENDING=0 (reserved)`, `ACTIVE=1`, `LATCHED=2`, `RECOVERED=3`, `CLEARED=4`
   * `occurrence_count` (u8): Saturating event qualification counter (0..255)
   * `report_counter` (u8): Modulo-256 sequence number for serialization
   * `flags` (u8): Bit 0 = `FIRST_LOCAL_ESTOP_CAUSE`, Bit 1 = `SNAPSHOT_VALID`
   * `snapshot_data` (u16): Quantitative diagnostic payload (decoded per-event, see §8.4)

2. **Real-Time State & System Diagnostics (`0x210 RT_STATE_RPT` @ 10 Hz / 100 ms, High + Low CAN):**
   * Operational Mode (`mode`: `0=Manual, 1=Auto, 2=Estop`)
   * Safety State (`safety_state`: `0=Normal, 1=InternalEstop [ramp/hold], 2=Fault`)
   * Reversing Status (`reversing`: bool)
   * High-Bus RX Overflow Count (`rx_overflow`: MCP2515 queue drop counter)
   * Latched ESTOP Reason (`estop_reason`: `1=Button, 2=Heartbeat, 3=FollowingError, 4=Obstacle, 5=CanEstop, 6=BusOff, 7=Internal, 8=EgasMismatch, 9=StaleCmd, 10=Watchdog`)
   * Steering State Machine Phase (`steer_state`: `0=BootWait, 1=ListenSync, 2=Active, 3=RampToZero, 4=HoldThenSilent, 5=Fault`)
   * Task Health Bitmask (`task_health`: Bit0=Control, Bit1=Dispatch, Bit2=TxLow, Bit3=TxHigh, Bit7=BenchBuild)

3. **Speed Loop Telemetry (`0x220 RT_PID_RPT` @ 10 Hz / 100 ms, High CAN):**
   * Commanded Speed Setpoint (`speed_setpoint_mmps`: int16, mm/s)
   * Actual Measured Vehicle Speed (`measured_speed_mmps`: int16, mm/s)
   * PID Speed Trim Correction Effort (`pid_output_mmps`: int16, mm/s)

4. **Steering Subsystem Diagnostics (`0x310 STEER_DIAG` @ 10 Hz / 100 ms, High CAN):**
   * Measured Steering Rack Angle (`angle_0_1deg`: 0.1° resolution, offset -300.0)
   * Steering Actuator Internal Fault Active (`fault`: bool)
   * Steering Motor Current Draw (`motor_current`: 0.01 A units)
   * Steering ECU Board Temperature (`ecu_temp`: 0.1 °C units)

5. **Braking Subsystem Diagnostics (`0x311 BRAKE_DIAG` @ 10 Hz / 100 ms, High CAN):**
   * Measured Hydraulic Line Pressure (`pressure_raw`: 0.05 bar/kPa units)
   * Brake Actuator Internal Fault Active (`fault`: bool)
   * Brake Motor Current Draw (`motor_current`: 0.01 A units)
   * Brake ECU Board Temperature (`ecu_temp`: 0.1 °C units)

6. **Coherent Measured Motion Report (`0x121 RT_MOTION_RPT` @ 100 Hz / 10 ms, High CAN):**
   * Forward Speed (`speed_mmps`: int16, mm/s)
   * Physical Gear (`gear`: `0=N, 1=D, 3=R`)
   * Speed Freshness Validity Flag (`speed_valid`: bool)
   * Physical Gear Validity Flag (`gear_valid`: bool)
   * Kinematic Estimated Yaw Rate (`yaw_rate_mrad_s`: int16, mrad/s)
   * Yaw Rate Validity Flag (`yaw_rate_valid`: bool)
   * Rolling Sequence Counter (`rolling_counter`: 0–255)

7. **Dual Heartbeat Diagnostics (`0x7FD RT_HEARTBEAT` @ 2 Hz / 500 ms, Independent High & Low):**
   * Monotonic Alive Counter (`alive_ctr`: 0–255)
   * Diagnostic Health Bitmask (`health_flags`: Bit0=HeartbeatOk, Bit1=EstopActive, Bit2=ModeAuto, Bit3=CanOk)

#### 8.3.3 MTR Node Channels
1. **Event-Driven Fault Reports (`0x631 MTR_DIAG_EVENT_RPT` on Low CAN, forwarded to High):**
   * Emits MTR diagnostic records using standard DLC-8 layout with live snapshot data

2. **Motor Actuator Feedback (`0x206 MTR_MOTOR_FBK` @ 50 Hz / 20 ms):**
   * Actuator speed (`actual_speed_mmps` int16), Engaged relay gear state (`gear_state`: `N=0, D=1, S=2, R=3`), Fault flags (`kMtrFaultEstopActive=0x01, kMtrFaultCmdTimeout=0x02, kMtrFaultStartupReady=0x10`)

3. **Throttle Actuator Status (`0x120 SYS_THROTTLE_STS` @ 100 Hz / 10 ms):**
   * Commanded speed setpoint (`speed_mmps` int16) reflecting live DAC output status

---

### 8.4 Snapshot decoding (registry-driven)

`snapshot_data` (bytes 6–7) is decoded **per-event from the `snapshot:` block in
`diagnostics.yaml`** — it is not a hand-maintained table here. Decoders for
C++/TS/Python and this document are generated from the same source, and
`DIAGNOSTICS_HASH` folds the snapshot semantics so two systems cannot agree on a
`DiagId` but disagree on bytes 6–7.

Encodings:

- **`U16`** — a scaled integer (e.g. elapsed silence in ms, angle error in 0.1°,
  pressure in kPa).
- **`BITFIELD16`** — named sub-fields (e.g. `(TEC<<8)|REC`).

Example registry entries:

```yaml
- id: 0x0102
  key: SYS_RT_HEARTBEAT_TIMEOUT
  snapshot: { encoding: U16, quantity: elapsed_silence, unit: ms, scale: 1, saturate: true }
- id: 0x0203
  key: RT_STEER_FOLLOWING_ERROR
  snapshot: { encoding: U16, quantity: absolute_angle_error, unit: "0.1deg", scale: 1 }
- id: 0x0204
  key: RT_CAN_BUS_OFF
  snapshot:
    encoding: BITFIELD16
    fields: [{ name: tec, bits: "15:8" }, { name: rec, bits: "7:0" }]
- id: 0x024A
  key: RT_CAN_INVALID_DLC
  snapshot:
    encoding: BITFIELD16
    fields: [{ name: expected_dlc, bits: "15:8" }, { name: actual_dlc, bits: "7:0" }]
```

The Host dictionary maps `DiagId → snapshot` metadata for display; embedded
firmware only needs the encoding to populate byte 6–7 from values already
available at the detection point.

---

Notes:
- `RT_HOST_HEARTBEAT_TIMEOUT` asserts `0x001` in firmware (`safety_monitor.h:116-124`
  + `rt-esp32/src/main.cpp:425`), but `docs/safety/estop.md:383` documents it as
  "controlled stop, not ESTOP". This is a **documentation/safety-review mismatch**;
  the registry records actual behavior (`ESTOP`). The behavior is **not** changed
  in Phase B.
- `MTR_THROTTLE_IMPLAUSIBLE` / `MTR_ADC_FAULT` / `MTR_GEAR_CONFLICT` correspond to
  dead `kMtrFault*` bits in `shared_config.h`; no production detector sets them.
  They are coverage entries, not live Phase-B events.
- Normal obstacle stopping (Host/Autoware distance → RT `PhysicsModel::obstacle_limit`
  → controlled deceleration) is **nominal operation**, not a diagnostic. The
  earlier `RT_OBSTACLE_ESTOP` entry has been **removed**; a diagnostic would only
  exist if an obstacle-input *fault* detector (timeout/invalid) is implemented
  later (see `RT_HOST_OBSTACLE_DISTANCE_TIMEOUT`).
- **`RT_DEV_JUMPER_TOGGLE_ACTIVE` is excluded from the live Phase-B catalog.**
  Firmware evidence shows RT reads `DEVELOPER_OVERRIDE_PIN=42`
  (`rt-esp32/src/main.cpp:836-847`), but hardware inventory attributes the
  developer jumper to SYS (`sys-esp32/src/main.cpp:1084-1095`). Physical
  observability is therefore **unproven**. It is kept as a coverage backlog item.
- `MTR_SHIFT_DWELL_ACTIVE` was removed: mandatory D↔R neutral dwell is normal protective behavior.
- **Audit Decisions & Categorization Rules:**
  - **OMITTED**: `MTR_CLOCK_CALIBRATION_DRIFT` (arbitration/queue latency contaminates CAN arrival timing), `SYS_CPU_CORE_SATURATED` (task-deadline/watchdog monitoring gives more actionable evidence with zero complexity), dynamic heap fragmentation under zero-runtime-allocation policy.
  - **UNOBSERVABLE**: Wheel slip, differential wheel speed, and kinematic yaw mismatch are unsupported by current single-encoder sensing without independent 4-wheel sensors or IMU.
  - **DEFERRED**: CAN rate/jitter analysis, stack high-water trend monitoring.
  - **PHASE C CANDIDATES**: `RT_MCP2515_SPI_COMM_FAIL`, `RT_CAN_HIGH_TX_FAILURE` (evidence-only), `RT_KINEMATIC_SOLVER_SINGULARITY` (`std::isfinite()` guard), brownout reset history, local ESTOP clear-block reason, qualified mode desync, pressure-build failure.

## 9. Phased implementation roadmap

### Phase B — Diagnostic Event Plane (Current Scope)
- **WP0 — Registry + wire contracts**: create `protocol/diagnostics/diagnostics.yaml`
  (v2 schema with `evidence_sources`, `snapshot:` blocks, IMPLEMENTED-only firmware
  enum); add an explicit diagnostics loader/validator to `protocol.py`; emit
  `DIAGNOSTICS_HASH` (folds snapshot semantics) + compact C++ (implemented-only
  enum + minimal metadata) / rich TS+Python metadata + coverage markdown; add
  `0x601/0x621/0x631` CAN messages, Low→High routes, payload vectors, and
  baseline-manifest updates. **No safety/control behavior changed**.
- **WP1 — DiagnosticManager**: multi-event active set; `raise(DiagId, snapshot)` /
  `recover` / `clear` / `pop_pending_report`; occurrence count; report counter;
  immediate state-change report; periodic active replay (~1 Hz max); per-ECU
  `first_local_estop_cause`. No reaction dispatch, no generic debounce, no new
  safety logic. Manager performs fixed-size bookkeeping only.
- **WP2–WP4 — Instrument existing detectors**: existing reaction executes exactly
  as today, then `diag.raise(DiagId, snapshot)` is called for
  `monitoring: IMPLEMENTED` events. No new fault detection.
- **WP5 — SES/SEB translation (canonical ownership)**: vendor/internal actuator
  faults have **one canonical translator** — **SYS for SEB**, **RT for SES**.
- **WP6 — Simulation**: same `DiagId`s and active-set behavior in the simulator.
- **WP7 — Coverage / sign-off**: IMPLEMENTED, NOT_IMPLEMENTED, genuinely
  UNOBSERVABLE; resolve open items.

### Phase C1 — Production Health & Reset Supervision (Future)
- **MCU Boot / Reset Cause Supervision**: Query `esp_reset_reason()` / STM32 `RCC->CSR` at boot; report `WATCHDOG_RESET`, `BROWNOUT_RESET`, `PANIC_RESET`.
- **WdgM-Style Alive & Deadline Supervision**: Control-loop deadline monitoring (`RT_CONTROL_DEADLINE_MISSED`), task stall supervision (`RT_TASK_STALL`), critical queue overflow (`RT_GATEWAY_QUEUE_OVERFLOW`).
- **Hardware Bus & Interface Faults**: `RT_MCP2515_SPI_COMM_FAIL`, evidence-only `RT_CAN_HIGH_TX_FAILURE`.

### Phase C2 — Physical Plausibility & State Machine Guarding (Future)
- **Critical Plausibility**: Uncommanded propulsion, `SYS_SEB_PRESSURE_BUILD_FAILURE`, `RT_STEER_HIGH_OPPOSING_LOAD`, `RT_CONTROL_OUTPUT_INVALID` (`std::isfinite()` output guard).
- **State Machine Guarding**: Qualified mode desync after handshake, local ESTOP clear-block reason.

### Phase C3 — Persistent Incident Memory (Future)
- **Per-ECU Retained NVM Ring Buffer**: 8–16 critical incident records stored across resets; RAM-first non-blocking writes via low-priority background task.
- **Host Historian**: Host stores full diagnostic history log across vehicle lifetime.

## 10. What Phase B explicitly is not

- Not a new safety controller. `DiagnosticManager` never asserts ESTOP, inhibits,
  or stops the vehicle.
- Not a new detector factory. Only existing verified detectors are instrumented in Phase B.
- Not a replacement for `0x001`/`0x011`. Those remain the safety state; diagnostics
  explain *why*.
- Not a CAN-contract rewrite. The three report messages follow the existing
  contract conventions exactly.
