# Pre-Hardware Software-Only Verification Gate & Testing Plan

## 1. Executive Summary & Purpose

Before flashing **SYS-ESP32**, **RT-ESP32**, or **MTR-STM32** hardware, and before connecting any physical actuators, the system requires a **software-only verification gate** significantly more rigorous than ordinary unit tests.

This test plan adheres to established embedded safety-critical software engineering practices:
- Formal requirements and state-machine transition testing
- Model-based and property-based differential testing
- Coverage-guided fuzzing and runtime sanitizers (ASan, UBSan, TSan)
- Static analysis and strict multi-compiler builds
- Fault injection and whole-vehicle Software-In-the-Loop (SIL) simulation
- Resource budgeting (stack, RAM, zero runtime heap allocation)
- Target cross-compilation for all embedded targets

The Phase B system architecture makes this unusually testable because diagnostics cannot create vehicle reactions, and `DiagnosticManager` must remain fixed-size and non-blocking with zero dynamic heap allocation, no synchronous CAN transmission, and no safety dispatch.

---

## 2. The Pre-Hardware Test Stack

### 2.1 Test Gate Summary

| Gate | Test Category | Scope / Focus | Importance |
| :---: | :--- | :--- | :---: |
| **1** | Exhaustive WP1 State-Machine Tests | Complete lifecycle contract & edge cases | **MANDATORY** |
| **2** | Python Reference-Model Differential Tests | Dual-run differential validation vs. C++ | **MANDATORY** |
| **3** | Property-Based Random Sequence Tests | Hypothesis-driven invariant verification | **MANDATORY** |
| **4** | ASan + UBSan Native Tests | Memory corruption, bounds, undefined behavior | **MANDATORY** |
| **5** | ThreadSanitizer / Concurrency Policy | Multitasking race-condition elimination | **MANDATORY if multi-task** |
| **6** | libFuzzer Protocol + Manager Fuzzing | Coverage-guided state & protocol fuzzing | **Very High** |
| **7** | Diagnostic Non-Interference Differential Test | Proves diagnostics do not alter vehicle outputs | **Critical** |
| **8** | Whole-Vehicle SIL Fault Injection | End-to-end multi-node simulated network | **Critical** |
| **9** | Multi-Compiler + Target Cross-Build | GCC, Clang, ESP-IDF, ARM toolchain builds | **Mandatory** |
| **10** | Static Analysis / Warnings-as-Errors | Zero warnings, clang-tidy, cppcheck | **Mandatory** |
| **11** | Mutation Testing | Test suite defect detection efficacy (>=90% kill) | **Very High** |
| **12** | Long-Duration Soak + Tick Wrap | Tick rollover (49.7 day wrap) & soak testing | **Very High** |
| **13** | Resource / Stack / Allocation Budgets | Zero malloc, RAM footprint, `.su` stack audit | **Embedded Mandatory** |
| **14** | Cross-Language Generated-Code Roundtrip | C++ <-> Python <-> TypeScript codec roundtrip | **High** |

### 2.2 Layered Defense Strategy

The strongest verification gate is not merely accumulating "more example tests." It is the layered synthesis of complementary verification techniques:

```text
       ┌───────────────────────────────────────────────┐
       │             EXHAUSTIVE TRANSITIONS            │
       └───────────────────────┬───────────────────────┘
                               │
       ┌───────────────────────▼───────────────────────┐
       │            REFERENCE MODEL (Python)           │
       └───────────────────────┬───────────────────────┘
                               │
       ┌───────────────────────▼───────────────────────┐
       │           PROPERTY-BASED TESTING              │
       └───────────────────────┬───────────────────────┘
                               │
       ┌───────────────────────▼───────────────────────┐
       │             FUZZING (libFuzzer)               │
       └───────────────────────┬───────────────────────┘
                               │
       ┌───────────────────────▼───────────────────────┐
       │           SANITIZERS (ASan, UBSan)            │
       └───────────────────────┬───────────────────────┘
                               │
       ┌───────────────────────▼───────────────────────┐
       │          FAULT-INJECTED VEHICLE SIL           │
       └───────────────────────┬───────────────────────┘
                               │
       ┌───────────────────────▼───────────────────────┐
       │          NON-INTERFERENCE TESTING             │
       └───────────────────────────────────────────────┘
```

---

## 3. Component & State-Machine Verification

### 3.1 Exhaustive State-Machine Testing (`DiagnosticManager`)

Do not just write 20 happy-path test cases. For WP1, enumerate the entire lifecycle contract across all four core states:

```text
CLEARED
ACTIVE
RECOVERED
LATCHED
```

against all lifecycle operations:

```text
raise
recover
clear
re-raise
replay
pop
ESTOP-episode-clear
```

Evaluate across at least three representative metadata classes:
- **Class A**: Non-latching warning
- **Class B**: Latching non-ESTOP event
- **Class C**: Latching ESTOP event

#### Explicit Test Sequences & Edge Cases
- **Valid State Transitions**:
  ```text
  CLEARED   → raise → ACTIVE
  RECOVERED → raise → ACTIVE
  LATCHED   → raise → ACTIVE
  ```
- **Idempotency & Re-raise**:
  ```text
  ACTIVE + repeated raise
      occurrence does not increment
      no duplicate state-change report
  ```
- **Recovery Policies**:
  ```text
  ACTIVE + recover(non-latching) → RECOVERED
  ACTIVE + recover(latching)     → LATCHED
  ```
- **Clearing Policies**:
  ```text
  RECOVERED/LATCHED + clear → CLEARED
  ACTIVE + clear            → no illegal clear
  ```
- **Counter Rollover & Saturation**:
  ```text
  occurrence:     254 → 255 → 255 (saturating at uint8 max)
  report_counter: 254 → 255 → 0   (wrapping uint8)
  ```
- **Snapshot Payload Boundaries**:
  ```text
  snapshot absent
  snapshot = 0
  snapshot = 65535
  ```
- **ESTOP Episode Origin Tracking**:
  ```text
  FIRST_LOCAL_ESTOP_CAUSE:
      first ESTOP cause           = 1
      second local ESTOP cause    = 0
      clear individual DTC        = still 0
      on_estop_episode_cleared()
      next ESTOP cause            = 1
  ```

> **Target Metric**: Achieve **100% branch and transition coverage** on `DiagnosticManager`, not merely line coverage.

---

### 3.2 Independent Python Reference Model (Differential Testing)

Do **not** use the C++ implementation itself to compute expected test values. Create an independent, minimal pure-Python reference model:

```python
class ModelDiag:
    state: str
    occurrence_count: int
    snapshot: int
    first_estop: bool
```

Drive both implementations concurrently through identical operation streams:

```text
          same operation sequence
                     ↓
      ┌─────────────┬───────────────┐
      │ C++ manager │ Python model  │
      └──────┬──────┴───────┬───────┘
             │              │
             └──── compare ─┘
```

Compare state after every single operation:
- `state`
- `occurrence_count`
- `snapshot`
- `snapshot_valid`
- pending reports
- flags
- `report_counter`
- report ordering

Differential testing ensures that an implementation bug does not silently become an accepted expected value in the test suite.

---

### 3.3 Property-Based Random Sequence Testing (Hypothesis)

Leverage Hypothesis integrated with pytest to generate arbitrarily randomized sequences of thousands of operations:

```text
raise A
raise C
recover A
raise B
clear A
pop
advance_time(783)
replay
recover C
pop
...
```

Execute thousands of generated traces against both the Python reference model and the C++ manager.

#### Invariant System Properties
- `occurrence_count` never decreases except upon defined clear/reset.
- `occurrence_count` never exceeds 255.
- `report_counter` increments exactly once per emitted record.
- No invalid or undefined state values can ever be assigned.
- Only the very first ESTOP cause in an episode receives the `FIRST_LOCAL_ESTOP_CAUSE` flag.
- Clearing a warning can never reset an active ESTOP episode.
- A repeated active `raise` does not increment occurrence counters.
- `replay` never changes diagnostic state.
- `replay` never modifies `occurrence_count`.
- Diagnostic invocations never produce vehicle actuation side-effects.

Hypothesis automatically shrinks complex, multi-step failures (e.g., a 237-operation sequence) into the minimal reproducible sequence:
```text
raise(C) → recover(C) → clear(C) → raise(A)
```

---

### 3.4 Mutation Testing (WP1 Test Suite Validation)

Ensure that the test suite actively detects implementation defects by introducing deliberate code mutations:
- `occurrence++` → removed
- `>= 255` → `> 255`
- `latching = true` → `false`
- `FIRST_LOCAL` flag condition inverted
- `clear` resets ESTOP episode logic
- `report_counter` increment omitted
- `RECOVERED` → `ACTIVE` transition corrupted

Every mutation must trigger a test failure.
- **Target**: Maintain a **>= 90% mutation kill rate** rather than relying solely on line coverage metrics.

---

## 4. Memory, Concurrency & Low-Level Safety

### 4.1 Native Sanitizer Suite (ASan + UBSan)

Passing standard native unit tests does **not** guarantee memory safety. Build production code natively with AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
-fsanitize=address,undefined -fno-omit-frame-pointer
```

This gate detects:
- Out-of-bounds array/buffer indexing
- Use-after-free conditions
- Invalid bit-shift operations
- Signed integer overflow undefined behavior
- Misaligned memory accesses
- Enum range violations
- Integer conversion/truncation defects

**Release Gate**: **0 sanitizer findings**. Because the architecture forbids heap allocation, ASan runs with negligible noise.

---

### 4.2 Concurrency Architecture & ThreadSanitizer (TSan)

`raise()` may be triggered from detector and control tasks, whereas `pop_pending_report()` runs inside the CAN TX task. The system must establish an explicit concurrency model prior to hardware testing:

- **Option A (Owning Task)**: `DiagnosticManager` has one owning task; all other tasks queue requests to the owner.
- **Option B (Thread-Safe API)**: The API is explicitly thread-safe using bounded critical sections or lock-free atomics.

Never allow "accidental" thread safety.

If **Option B** is implemented:
1. Compile a dedicated host build with:
   ```bash
   -fsanitize=thread
   ```
2. Execute a multi-threaded stress test for millions of operations:
   - Producer Thread 1 → `raise`/`recover` event A
   - Producer Thread 2 → `raise`/`recover` event B
   - Consumer Thread   → `pop`/`replay`
3. **Hard Blocker**: Any TSan data race is a release blocker prior to running on dual-core ESP32 targets.

---

### 4.3 Runtime Allocation Prohibition (Zero-Heap Trap)

Phase B strictly requires fixed-size bookkeeping with zero dynamic memory allocation. Validate this constraint programmatically:

```text
manager constructed
       ↓
forbid operator new / malloc
       ↓
execute 1,000,000 manager operations
```

**Assertion**: Exactly **0 runtime heap allocations**.

Enforce compile-time RAM budgets:
```cpp
static_assert(sizeof(DiagnosticManager) <= MAX_DIAG_RAM);
```
Apply the same compile-time budget verification to fixed report queue sizes.

---

### 4.4 Target Stack Usage & Resource Budgets

Cross-compile production firmware with stack usage reporting:
```bash
-fstack-usage
```

Inspect generated `.su` files for core routines:
- `raise()`
- `recover()`
- `clear()`
- `pop_pending_report()`
- `encode_diag()`

All stack frames must remain bounded and minimal. Establish a regression budget and continuously inspect memory section deltas:
- `.flash`
- `.data`
- `.bss`

---

## 5. Protocol, Fuzzing & Stress Testing

### 5.1 Coverage-Guided Fuzzing (libFuzzer)

`DiagnosticManager`'s compact, stateful design is ideally suited for coverage-guided fuzzing. Map arbitrary raw byte streams directly to API operations:
- `byte 0`: operation (`raise`, `recover`, `clear`, `pop`, `replay`, `episode_clear`)
- `byte 1`: `DiagId` index
- `byte 2-3`: snapshot payload
- `byte 4-7`: time advance delta

Compile with:
```bash
-fsanitize=fuzzer,address,undefined
```
Run with assertions enabled:
- **Short fuzz runs**: Executed on every pull request / commit
- **Long fuzz runs**: Run nightly in CI
- **Seed Corpus**: Must include all 42 implemented diagnostic IDs

---

### 5.2 CAN Decoder Robustness Fuzzing

Before physical bus connection, stream randomized CAN data into all system decoders:
- SYS CAN decoder
- RT low CAN decoder
- RT high CAN decoder
- MTR CAN decoder
- Host diagnostic decoder

Feed arbitrary combinations of:
- Arbitrary CAN ID
- DLC values from 0 to 15
- 8 arbitrary payload bytes
- Randomized arrival timestamps

#### Decoder Invariants Under Fuzzing
- System never crashes or faults.
- Decoders never read memory outside the declared frame DLC.
- Unknown CAN IDs are safely ignored.
- Invalid DLC and corrupted CRC frames are rejected.
- Stale or corrupted rolling counters do not refresh node freshness deadlines.
- Malformed diagnostic frames cannot compromise safety or control state.
- Unknown `DiagId` values are handled gracefully and rendered numerically without exceptions.
- Reserved protocol states are rejected and flagged.

---

### 5.3 Simultaneous Saturation Test (All 42 Diagnostics)

Stress test the system under full saturation by activating all 42 implemented `DiagId`s concurrently:
- Verify absence of internal array or index overflows.
- Ensure dense index mapping is strictly bijective:
  ```cpp
  static_assert(kImplementedDiagCount == 42);
  assert(diag_index(meta[i].id) == i);
  ```
- Confirm independent preservation of all event states, occurrence counters, and snapshots.
- Verify periodic replay traverses the entire active set without starvation.
- Recover and clear events in randomized permutations.

---

### 5.4 Pending-Report Queue Capacity & Overflow Testing

Deliberately exhaust the fixed transition queue to verify deterministic saturation handling:

```text
capacity = 16

generate 16 reports → all retained
generate #17        → exact documented overflow policy
```

Establish explicit policy requirements:
- Overflow behavior must be deterministic: drop newest, drop oldest, or coalesce by `DiagId`.
- The overflow sticky flag must be asserted.
- **Safety Invariant**: Under a permanently saturated diagnostic queue:
  - The ESTOP safety path remains fully operational.
  - Actuation and vehicle control loops run without interruption.
- If using a single pending-report bit per event, explicitly test rapid `ACTIVE` → `RECOVERED` transitions before TX draining to ensure state changes are not dropped.

---

### 5.5 Simulated Time Wraparound (Tick Rollover)

For 32-bit millisecond tick counters (`uint32_t`), rollover occurs every ~49.7 days. Do not wait 49 days; test boundary conditions directly:

```text
last = 0xFFFFFFF0
now  = 0x00000020
```

Verify rollover arithmetic and boundary thresholds (`T - 1`, `T`, `T + 1`) across:
- Timeout duration calculations
- Periodic active diagnostic replay intervals
- Heartbeat freshness and loss detectors
- Safety deadline monitors

---

## 6. Integration, SIL Simulation & Target Compilation

### 6.1 Diagnostic Non-Interference Differential Test

Directly validate the architectural mandate: **"Diagnostics explain behavior; diagnostics do not create behavior."**

Execute identical simulated driving and fault scenarios twice:
- **RUN A**: `DiagnosticManager` disabled / no-op
- **RUN B**: `DiagnosticManager` fully active

Compare all non-diagnostic externally observable outputs:
```text
CAN Frames:
  0x001, 0x011, 0x110, 0x113, 0x204, 0x169, 0x7B9

State & Control Outputs:
  mode state
  power state
  brake command
  steering command
  motor command
```

**Requirement**: Operational outputs must be **bit-for-bit identical** between Run A and Run B. Only diagnostic frames (`0x601`, `0x621`, `0x631`) may differ.

#### Intentional Diagnostic Fault Injection
Induce faults in the diagnostic pipeline:
- Queue saturation / report dropping
- Host disconnection
- Serialization failures
- Replay delays

Verify that vehicle control and safety responses remain 100% unaffected.

---

### 6.2 Whole-Vehicle Software-In-The-Loop (SIL) Simulation

Before connecting physical ECUs, execute full multi-node software simulation across a deterministic virtual CAN bus:

```text
             ┌─────────────────────────┐
             │     Host Simulator      │
             └────────────┬────────────┘
                          │
             ┌────────────┴────────────┐
             │    RT Software Model    │
             └────────────┬────────────┘
                          │
             ┌────────────┴────────────┐
             │   SYS Software Model    │
             └────────────┬────────────┘
                          │
             ┌────────────┴────────────┐
             │   MTR Software Model    │
             └────────────┬────────────┘
                          │
             ┌────────────┴────────────┐
             │ SES/SEB Simulated Nodes │
             └─────────────────────────┘
```

#### Fault Injection Suite
Inject anomalous network conditions:
- Normal operational traffic baseline
- Missing, duplicate, or reordered frames
- Invalid DLC, bad CRC, corrupted payload bytes
- Frozen rolling counters, counter jumps, counter rollover (`255` → `0`)
- Node resets and delayed frame transmissions
- CAN bus-off events and TX/RX queue overflows
- Simulated subsystem faults: SES fault, SEB fault, MTR timeout, Host loss, SYS loss, RT loss
- Multi-node simultaneous fault scenarios

#### Verification Criteria
1. Existing vehicle safety and control responses execute deterministically and remain unchanged.
2. Expected `DiagId`s are produced with accurate snapshots and correct causal mappings.

---

### 6.3 Multi-Compiler Host Builds & Strict Flags

Compile host test binaries with both **GCC (C++17)** and **Clang (C++17)** using strict compiler warning flags:

```bash
-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wswitch-enum -Wundef -Werror
```

Apply embedded-like constraints where applicable:
```bash
-fno-exceptions -fno-rtti
```

---

### 6.4 Static Analysis

Enforce clean static analysis prior to hardware deployment:
- `clang-tidy`
- `cppcheck`
- Zero compiler warnings (`-Werror`)

Key focus areas:
- Buffer bounds and memory safety
- Integer conversion and narrowing promotions
- Uninitialized variable usage
- Switch exhaustiveness across all enum members
- Dead code elimination and variable lifetimes

---

### 6.5 Full Cross-Compilation of All Embedded Targets

Validate that all target firmware builds compile cleanly on host CI without requiring physical hardware:

```text
ESP32-S3 SYS → Full ESP-IDF build
ESP32-S3 RT  → Full ESP-IDF build
STM32G431    → Full arm-none-eabi build
```

This verifies:
- Target-specific SDK headers and macros
- Memory alignment and struct packing
- FreeRTOS API compatibility
- ESP-IDF build system configurations
- 32-bit architectural constraints
- STM32 peripheral driver compatibility

---

### 6.6 Cross-Language Protocol Roundtrip Testing

Generated protocol code spans C++, TypeScript, and Python:

```text
C++ encode    → Python decode → verify identical
Python encode → C++ decode    → verify identical
```

Verify boundary values:
- `0`, `1`, `255`, `256`, `65535`
- All state enum definitions
- Bitfield flag combinations
- Big-endian snapshot field encoding/decoding

Confirm semantic synchronization with `DIAGNOSTICS_HASH`: both host and embedded targets agree on semantics, not just hash strings.

---

---

## 7. Dedicated Pre-Hardware ESTOP & Safety Reset Verification Suite

ESTOP assert, clear authorization, rearm, and reset behaviors must form their own **dedicated pre-hardware verification suite**, separate from general diagnostic tests.

### 7.1 Fundamental State Distinctions

The test suite must strictly enforce and validate the boundaries between five distinct operations:

```text
ESTOP ASSERT
    ≠
ESTOP CLEAR AUTHORIZATION
    ≠
DIAGNOSTIC clear()
    ≠
VEHICLE REARM
    ≠
ECU reboot / reset
```

The system architecture explicitly separates diagnostic clearing from the Phase-A ESTOP episode:
- `DiagnosticManager::clear()` only clears a diagnostic record / DTC.
- `DiagnosticManager::on_estop_episode_cleared()` performs local bookkeeping only after the real vehicle safety clear and rearm sequence has completed.

---

### 7.2 Core ESTOP Assertion Paths

Test every valid assertion path independently across all nodes and buses:

```text
0x001 received on High
→ RT ESTOP immediately
→ RT forwards to Low
→ SYS / MTR inhibit

0x001 received on Low
→ RT ESTOP immediately
→ RT forwards to High
→ Host sees it

SYS local ESTOP button
→ SYS asserts 0x001
→ ESTOP

RT local safety cause
→ RT asserts 0x001 on both buses
→ ESTOP

MTR local ESTOP-grade cause
→ MTR asserts 0x001
→ system ESTOP

0x011 estop_active=1
→ RT / MTR latch / inhibit immediately
```

#### Multiple Simultaneous Assertors
Simulate concurrent assertion from multiple origins:
```text
SYS + RT assert 0x001 in the same episode
RT + MTR assert
Host + SYS assert
All nodes assert simultaneously
```
**Expected Invariant**: No conflict, no dependence on sender identity, and no ping-pong/echo loop.

---

### 7.3 Assert Dominance Over Clear

The test suite must enforce this hard invariant:

```text
ASSERT DOMINANCE:
Trusted assertion evidence always has higher priority than clear/rearm evidence.
If ASSERT and CLEAR evidence occur simultaneously → ASSERT wins.
```

Explicitly inject assertion events at every stage of the clearing sequence:
- `0x001` arrives during clear sequence
- `0x011 estop_active=1` arrives during clear sequence
- Local fault condition reappears during clear sequence
- Physical ESTOP button pressed during clear sequence

**Expected Invariant**: System aborts the clear sequence immediately and remains latched in ESTOP.

---

### 7.4 Exhaustive Asymmetric `0x011` Clear Rule Verification

Test the multi-frame clear authorization rule required upon boot or stream reacquisition:

```text
After boot / reacquisition:
    first valid 0x011 → baseline only
```

#### Baseline Scenarios
```text
baseline = (estop_active == 1)
→ latch immediately
→ clear_count = 0

baseline = (estop_active == 0)
→ DO NOT clear
→ clear_count = 0
```

#### Clear Eligibility Progression
```text
baseline zero (fresh frame #0)
      ↓
fresh zero #1 (sequential counter, valid CRC)
      ↓
fresh zero #2 (sequential counter, valid CRC)
      ↓
CLEAR AUTHORIZATION ELIGIBLE (not before!)
```

#### Anomaly and Error Injection
- **Duplicate Frame**: Zero baseline → duplicate zero → zero → **Clear rejected**.
- **Counter Jump**: Zero baseline → rolling counter jump → zero → zero → **Sequence reacquisition required**.
- **CRC Error**: Zero baseline → corrupted CRC → zero → zero → **Clear rejected**.
- **Stream Timeout**: Zero baseline → timeout gap → zero → zero → **Clear rejected until reacquired**.
- **Relatch During Clear**: Zero baseline → fresh zero #1 → `estop_active == 1` → **Immediately re-latch and reset clear sequence to 0**.
- **Rolling Counter Rollover**: Verify `254 → 255 → 0 → 1` is recognized as a valid advancing sequence.

---

### 7.5 Reboot Behavior During ESTOP

Reboot bugs in safety controllers are catastrophic. Validate all reboot combinations while an active ESTOP condition exists:

#### SYS Reboot During ESTOP
```text
ESTOP active
→ SYS resets
→ SYS boots inhibited (never enters NORMAL automatically)
→ Emits estop_active=1 while safety sources are reacquired
→ Cannot clear merely because RAM was reset
```

#### RT Reboot During ESTOP
```text
RT resets
→ No stale pre-reset authority is trusted
→ Propulsion remains inhibited
→ RT reacquires 0x011
→ First zero frame is treated strictly as baseline
→ Clear requires fresh, consecutive post-reboot evidence
```

#### MTR Reboot During ESTOP
```text
MTR reboot
→ Motor output remains inhibited
→ Old 0x204 drive command cannot resurrect propulsion
```

#### Pairwise & Multi-ECU Concurrent Reboots
Execute concurrent reset combinations:
- SYS + RT reboot
- RT + MTR reboot
- SYS + MTR reboot
- All three ECUs reboot simultaneously

> **Core Invariant**: **No ECU reboot can be interpreted as an ESTOP clear or propulsion authorization.**

---

### 7.6 "Clear Must Not Mean Drive" & Authority Lifecycle

A successful ESTOP clear must transition the vehicle into an inhibited state requiring explicit rearm—propulsion must never resume automatically.

#### Pre-ESTOP State
```text
Mode: AUTO
Power: ON
Drive Command: 2000 mm/s (0x204)
```
→ Trigger ESTOP assertion.

#### Post-Clear State
```text
Expected State:
    SAFE_IDLE / REARM_REQUIRED

Prohibited Actions:
    DO NOT resume old AUTO command
    DO NOT resume power authorization
    DO NOT resume 2000 mm/s propulsion
```

#### Deliberate Stale Frame Replay
Replay cached pre-ESTOP frames immediately after clear:
- Replay old `0x110` (Mode)
- Replay old `0x113` (Power)
- Replay old `0x204` (Drive)

**Expected Outcome**: Vehicle remains strictly inhibited.

Propulsion eligibility may only be restored when the full post-clear sequence occurs:
```text
NEW post-clear START / rearm edge
               +
NEW fresh mode authority (0x110)
               +
NEW fresh power authority (0x113)
               +
NEW fresh drive authority (0x204)
```

---

### 7.7 Stale Authority Resurrection Prevention

Simulate the chronological timeline:
```text
t0: AUTO mode active
t1: Power ON active
t2: Drive command = 3000 mm/s
t3: ESTOP asserts
t4: Clear authorization succeeds
t5: No new commands sent
```
**Assertion**: Zero propulsion permitted indefinitely.

Then inject partial authority updates:
- Inject old duplicate `0x110`, `0x113`, `0x204` → **No propulsion**
- Inject fresh `0x110` only → **No propulsion**
- Inject fresh `0x113` only → **No propulsion**
- Inject fresh `0x204` without rearm edge → **No propulsion**

Only the complete, ordered post-clear authority sequence restores drive eligibility.

---

### 7.8 Fault Persistence & Return Edge Cases

#### Clear Attempted While Cause Remains Active
Examples:
- Physical ESTOP button remains depressed
- RT heartbeat still absent
- Steering following error active
- MTR acknowledge failure active
- CAN bus-off state active

Transmit valid `0x011 estop_active=0` clear authorization from SYS:
```text
SYS clear authorization
           +
Local causes gone (UNMET)
           +
Local safety checks (UNMET)
           ↓
CLEAR REJECTED → Vehicle remains in ESTOP
```

#### Cause Returning Immediately After Clear
1. Cause disappears → Clear succeeds → State enters `SAFE_IDLE / REARM_REQUIRED` → Cause reappears prior to rearm → **ESTOP immediately reasserts**.
2. Cause disappears → Clear succeeds → Rearm succeeds → Fresh authority granted → Cause reappears → **ESTOP immediately reasserts**.

---

### 7.9 Gateway Forwarding & Echo Prevention (`0x001`)

For RT dual-bus gateway bridging:
- Frame `0x001` received on High bus → Forward **exactly once** to Low bus.
- Frame `0x001` received on Low bus → Forward **exactly once** to High bus.
- Locally generated `0x001` on RT → Transmit once on High, once on Low.

Connect to a virtual CAN bus configured to reflect frames back:
**Assertion**: Zero infinite bridge echoes, zero frame amplification loops, zero `0x001` CAN storms.

---

### 7.10 Safety & Diagnostic Independence in ESTOP

Execute every ESTOP, clear, and reset scenario twice:
- **Run A**: `DiagnosticManager` disabled / no-op
- **Run B**: `DiagnosticManager` enabled

Compare:
- Frame outputs: `0x001`, `0x011`, `0x110`, `0x113`, `0x204`, `0x169`, `0x7B9`
- Internal states: safety state, motor inhibit, steering state, brake state

**Requirement**: All non-diagnostic outputs and safety states must be bit-for-bit identical. Only diagnostic telemetry frames (`0x601`, `0x621`, `0x631`) may differ.

---

### 7.11 Diagnostic ESTOP Episode Bookkeeping (WP1)

Verify diagnostic metadata tracking independently from safety actuation:
```text
Cause A triggers ESTOP
→ Cause A flagged with FIRST_LOCAL_ESTOP_CAUSE

Cause B triggers ESTOP during the same episode
→ Cause B does NOT receive FIRST_LOCAL_ESTOP_CAUSE

diag.clear(A) called
→ Does NOT reset first-cause ownership

diag.clear(B) called
→ Does NOT reset first-cause ownership

Vehicle Phase-A clear + rearm completes
→ DiagnosticManager::on_estop_episode_cleared() invoked

Next ESTOP cause C triggers
→ Cause C receives FIRST_LOCAL_ESTOP_CAUSE
```

The flag is local per ECU and strictly resets only when the Phase-A physical clear/rearm sequence has successfully finished.

---

### 7.12 Explicit Safety Scenario Test Suite

Implement the following concrete test suite:

```text
test_estop_001_high_to_low()
test_estop_001_low_to_high()
test_estop_multiple_assertors()

test_safety_sts_assert_first_frame()
test_safety_sts_zero_baseline_does_not_clear()
test_safety_sts_two_fresh_zero_frames_clear_authority()
test_safety_sts_duplicate_does_not_advance_clear()
test_safety_sts_crc_error_does_not_advance_clear()
test_safety_sts_counter_fault_resets_clear_sequence()
test_safety_sts_timeout_resets_clear_sequence()
test_safety_sts_counter_wrap()

test_estop_assert_wins_during_clear()
test_estop_cause_still_active_blocks_clear()

test_sys_reboot_does_not_clear_estop()
test_rt_reboot_does_not_clear_estop()
test_mtr_reboot_does_not_clear_estop()
test_all_ecus_reboot_safe()

test_clear_enters_rearm_required()
test_pre_estop_mode_authority_invalid_after_clear()
test_pre_estop_power_authority_invalid_after_clear()
test_pre_estop_drive_authority_invalid_after_clear()
test_fresh_authority_without_rearm_does_not_drive()
test_rearm_without_fresh_authority_does_not_drive()
test_full_post_clear_rearm_sequence_allows_drive()

test_gateway_no_estop_echo_loop()

test_diagnostics_enabled_does_not_change_safety_outputs()
```

---

### 7.13 Randomized Property-Based Safety Invariants

Run randomized sequence testing over the safety state space:
- `ASSERT`
- `CLEAR_AUTH`
- `0x011` valid / corrupted frames
- Rolling counter increments / jumps
- CRC corruptions
- Heartbeat dropouts / recoveries
- ECU resets / reboots
- START edge assertions
- Mode, power, and drive commands

The system must satisfy these eight global invariants across millions of cycles:

```text
P1: Reset / reboot never clears ESTOP by itself.
P2: Stale authority never survives an ESTOP event.
P3: Clear never enables propulsion directly.
P4: Assertion evidence always dominates clear evidence.
P5: Invalid or stale 0x011 never authorizes clear.
P6: No propulsion is possible until post-clear rearm AND fresh authority are granted.
P7: Diagnostics cannot alter vehicle safety behavior.
P8: Frame 0x001 cannot generate gateway echo storms.
```

---

## 8. Closed-Loop Physics SIL & Plant Simulation Suite

Software-in-the-loop testing must not terminate at CAN codec interfaces. The pre-hardware verification gate requires a **closed-loop physics SIL and vehicle plant simulation**.

This test answers the foundational question:
> *"If Host requests 3 m/s, what CAN frames are produced, what motor command reaches MTR, what wheel torque results, how fast does the vehicle actually accelerate, and does measured feedback converge to the requested motion within an allowed physical error envelope?"*

### 8.1 End-to-End Closed-Loop Signal Chain

The simulation exercises the unbroken software and physical control loop:

```text
HOST COMMAND
    ↓
0x300 Drive Request
    ↓
RT Command Validation / Kinematics / Speed PID Loop
    ↓
0x204 RT_DRIVE_CMD
    ↓
MTR Command Validation
    ↓
DAC / Direction Relays
    ↓
MOTOR MODEL (torque/lag response)
    ↓
Gearbox / Wheel Transmission
    ↓
Tractive Force (F_drive)
    ↓
VEHICLE PHYSICS (mass, drag, grade, rolling resistance)
    ↓
Actual Vehicle Speed (v)
    ↓
Encoder Model (quantization, resolution)
    ↓
0x206 MTR_MOTOR_FBK
    ↓
RT Closes the Speed Control Loop
```

```text
            ┌────────────────────────────────────────────────────────┐
            │                                                        ↓
Host ──→ RT ──→ MTR ──→ MOTOR MODEL ──→ VEHICLE DYNAMICS ──→ ENCODER MODEL
          ↑                                                      │
          └───────────────────── 0x206 FBK ──────────────────────┘
```

---

### 8.2 Deterministic Vehicle Longitudinal Plant Model

For longitudinal dynamics, the core model evaluates tractive and resisting forces at each step:

$$
F_\text{drive} = \frac{T_\text{wheel}}{r_w}
$$

Opposing environmental and passive forces:

$$
F_\text{roll} = C_{rr} m g \cos	heta
$$

$$
F_\text{grade} = m g \sin	heta
$$

$$
F_\text{drag} = \frac{1}{2} 
ho C_d A v^2
$$

Braking force:
$$
F_\text{brake}
$$

Net force balance and acceleration:

$$
F_\text{net} = F_\text{drive} - F_\text{roll} - F_\text{grade} - F_\text{drag} - F_\text{brake}
$$

$$
a = \frac{F_\text{net}}{m}
$$

Numerical state integration at each simulation step ($\Delta t = 0.01\text{ s}$ for 100 Hz RT loop):

$$
v_{k+1} = v_k + a_k \Delta t
$$

$$
x_{k+1} = x_k + v_k \Delta t + \frac{1}{2} a_k \Delta t^2
$$

---

### 8.3 Non-Instantaneous Motor & Traction Response Model

A simulated motor must never instantaneously adopt commanded velocities. Instantaneous transitions mask controller instability and integrator windup.

#### First-Order Dynamic Motor Lag
$$
\tau_m \dot{\omega} + \omega = K_m u
$$

In discrete simulation steps:

$$
\omega_{k+1} = \omega_k + \frac{\Delta t}{\tau_m} (K_m u_k - \omega_k)
$$

Where:
- $u$: DAC voltage / throttle command
- $K_m$: Steady-state motor gain
- $\tau_m$: Motor / controller mechanical time constant

Coupled through the drivetrain:

$$
v = r_w \omega_\text{wheel}, \quad \omega_\text{wheel} = \frac{\omega_\text{motor}}{G}, \quad T_\text{wheel} = T_\text{motor} G \eta
$$

#### Torque- and Power-Limited Motor Model
At low speeds, torque is bounded by maximum motor current; at high speeds, output is power-limited:

$$
T_m = \min\left(T_\text{cmd}, \; T_\text{max}, \; \frac{P_\text{max}}{\max(\omega, \omega_\epsilon)}
\right)
$$

Wheel driving force:

$$
F_\text{drive} = \frac{T_m G \eta}{r_w}
$$

This ensures an aggressive step command produces physically bounded acceleration rather than teleporting the vehicle to target speed.

---

### 8.4 Production Code In-The-Loop (No MTR Bypass)

The physics SIL plant must **never bypass MTR firmware**:

```text
INCORRECT BYPASS:
Host speed ──────────────────────────→ Simulated Plant Speed

CORRECT CLOSED-LOOP SIL:
Host speed
   ↓
Real RT command logic & PID
   ↓
Real 0x204 serializer
   ↓
Real MTR validation & safety gates
   ↓
Real MTR throttle / DAC conversion
   ↓
Simulated Motor Actuator Model
   ↓
Simulated Vehicle Longitudinal Plant
```

This catches scaling bugs, inverted unit signs, gear ratio mismatches, clamping errors, integer truncation, CAN endianness mistakes, DAC voltage scaling errors, and PID derivative spikes.

---

### 8.5 Simulated Feedback via Production CAN Interface

The plant computes `actual_speed`, `actual_gear`, and `fault_flags`, serializes them into genuine `0x206 MTR_MOTOR_FBK` frames, and injects them into RT's RX buffer. RT executes identically whether coupled to physical hardware or the simulation plant.

---

### 8.6 Parameter Uncertainty & Monte Carlo Tolerance Matrix

Individual physical parameters must be stressed across independent tolerance intervals:

| Parameter | Nominal Value | Simulation Tolerance Envelope |
| :--- | :---: | :---: |
| **Vehicle mass ($m$)** | Calculated design mass | $\pm 10\text{ to }15\%$ |
| **Wheel radius ($r_w$)** | Measured rolling radius | $\pm 1\text{ to }2\%$ |
| **Gear ratio ($G$)** | Nominal transmission ratio | $\pm 0.5\%$ |
| **Drivetrain efficiency ($\eta$)** | Nominal efficiency | $\pm 5\text{ to }10\%$ |
| **Rolling resistance ($C_{rr}$)** | Nominal tire resistance | $\pm 20\text{ to }30\%$ |
| **Motor gain ($K_m$)** | Measured/calculated | $\pm 5\text{ to }10\%$ |
| **Motor time constant ($\tau_m$)** | Nominal time constant | $\pm 20\%$ |
| **Encoder scaling** | Nominal pulse/rev | $\pm 1\%$ |
| **Encoder noise** | 0 | Small random Gaussian jitter |
| **CAN bus latency** | Nominal schedule | $\pm 1\text{ to }3\text{ ms}$ jitter |
| **Control loop jitter** | 10 ms nominal | Small scheduling jitter |
| **Battery bus voltage** | Nominal pack voltage | Full discharge-to-charge range |
| **Road grade ($	heta$)** | $0^\circ$ | $\pm 0^\circ\text{ to }15^\circ$ slope sweeps |

#### Monte Carlo Stability Runs
Execute 10,000 randomized parameter sweeps (e.g., light mass + low rolling resistance; heavy mass + low battery + steep incline) to confirm stability across the full parametric envelope.

---

### 8.7 Dynamic Pass/Fail Performance Envelopes

Do not assert exact point-by-point trajectory equality. Define bounding performance corridors:

```text
Example: Target Step from 0 to 3000 mm/s

PASS CRITERIA:
  - Final steady-state speed: v_final ∈ [2850, 3150] mm/s (±5%)
  - Peak transient speed: v_peak < 3300 mm/s (< 10% overshoot)
  - Settling time: t_settle < 4.0 s
  - No negative speed excursion during forward acceleration
  - Acceleration: a(t) ≤ calculated physical maximum
  - Jerk: da/dt bounded within passenger comfort limits
  - Command saturation: DAC command remains within valid range (0 - 4095)
  - Limit cycling: Steady-state oscillation amplitude below defined threshold
  - No NaN or Inf in floating/fixed-point variables
```

---

### 8.8 Essential Longitudinal Test Scenarios

1. **Straight-Line Acceleration Sweeps**:
   - $0 	o 500\text{ mm/s}$
   - $0 	o 1000\text{ mm/s}$
   - $0 	o 2000\text{ mm/s}$
   - $0 	o \text{maximum governed velocity}$
   - Evaluate rise time, acceleration profiles, DAC command curve, and peak wheel torque.

2. **Controlled Deceleration Steps**:
   - $3000 	o 2000\text{ mm/s}$
   - $3000 	o 1000\text{ mm/s}$
   - $3000 	o 0\text{ mm/s}$
   - Verify controller does not demand impossible negative torque unless regenerative braking is explicitly configured.

3. **Directional Reversal Under Speed**:
   - $+1000\text{ mm/s} 	o \text{Reverse}$
   - **Sequence Invariant**: Decelerate to zero $	o$ verify standstill $	o$ enter neutral dwell interval $	o$ switch direction relays $	o$ apply reverse drive command.
   - **Strictly Prohibited**: Engaging reverse direction relays while vehicle retains forward velocity.

4. **Hill Start & Gradeability Holds**:
   - Test slopes at $	heta = 5^\circ, \; 10^\circ, \; 15^\circ$.
   - Evaluate whether $F_\text{drive} > m g \sin	heta + F_\text{roll}$.
   - Confirm plant accurately halts or exhibits backward roll when available torque is insufficient.

---

### 8.9 Direct Motor Sizing & Torque Margin Verification

Directly test motor sizing calculations inside the simulation loop:

$$
T_\text{required} = (F_\text{accel} + F_\text{roll} + F_\text{grade} + F_\text{drag}) r_w
$$

Verify dynamic torque margin:

$$
\text{Margin} = \frac{T_\text{available}}{T_\text{required}} > 1.0
$$

Verify that under compound worst-case variations (+10% mass, low tire pressure, +2° slope, -10% efficiency, low battery voltage), the sizing margin remains safely above 1.0.

---

### 8.10 PID Robustness Under Plant Uncertainty

Subject the production RT PID controller to extreme parameter perturbations without recalibration:
- Heavy vs. light vehicle inertia
- Slow motor ($\tau_m + 20\%$) vs. fast motor ($\tau_m - 20\%$)
- Incline transitions (uphill to downhill)

Monitor for:
- Integrator windup on sustained slope climbs
- High-frequency limit-cycle oscillation around setpoints
- Sluggish settling time or instability caused by phase lag

---

### 8.11 Actuator Saturation & Anti-Windup

Real hardware has physical ceiling limits. Model:
- DAC voltage min/max clamp
- Motor phase current limits
- Maximum wheel torque bounds
- Battery voltage collapse under peak current load

**Anti-Windup Test**: Demand an impossible 10 m/s climb up a steep grade $	o$ PID output saturates $	o$ abruptly reduce setpoint to achievable 2 m/s.
**Assertion**: Controller must rapidly desaturate and resume linear regulation within $< 500\text{ ms}$ without lingering integrator overshoot.

---

### 8.12 Quantization & Discrete Signal Effects

Replace continuous double-precision math with exact integer quantization mirrors:
- `int16_t` velocity in mm/s
- 12-bit DAC integer steps ($0 - 4095$)
- Encoder discrete pulse counts per control tick
- CAN frame fixed-point field scalings

Validates low-speed deadbands, limit cycling, and velocity discretization noise.

---

### 8.13 Distributed Latency & Delay Margin Stress

Simulate cumulative distributed pipeline latency:
```text
Host compute → High CAN → RT task delay → Low CAN → MTR decode → DAC slew → Motor lag → Encoder sampling → Low CAN (0x206) → RT PID
```
- Nominal baseline: $10 - 30\text{ ms}$ round-trip
- Latency stress: Inject $+10\text{ ms}$, $+20\text{ ms}$, and $+50\text{ ms}$ synthetic delays
- Determine exact stability boundaries and empirical phase/delay margins.

---

### 8.14 ESTOP Physical Consequence & Stopping Distance SIL

Couple the ESTOP suite directly to the longitudinal plant. At $v = 4000\text{ mm/s}$, trigger `0x001 ESTOP`:
1. RT disables drive command and asserts brake command.
2. MTR shuts off propulsion DAC output within deadline.
3. Mechanical brake force builds up on simulated wheels.
4. Measure and record:
   - Time from `0x001` assertion to zero propulsion torque.
   - Time from `0x001` assertion to full brake force buildup.
   - Total stopping time ($t_\text{stop}$).
   - Total stopping distance ($d_\text{stop}$).
   - Peak deceleration ($a_\text{peak}$).

Compare directly against design equation bounds:

$$
d_\text{stop} = v_0 t_\text{reaction} + \frac{v_0^2}{2 a_\text{brake}}
$$

Validates both software protocol propagation and physical safety outcomes in a single unified test.

---

### 8.15 Test Suite Organization & Fidelity Tiers

#### Suggested Directory Hierarchy
```text
tests/physics/
    longitudinal_plant.py
    motor_model.py
    brake_model.py
    encoder_model.py
    virtual_can.py

    test_acceleration.py
    test_deceleration.py
    test_reverse.py
    test_gradeability.py
    test_pid_robustness.py
    test_estop_stopping.py
    test_latency_margin.py
    test_quantization.py
    test_monte_carlo.py
```

#### Fidelity Tiers
- **Level 1 (Algebraic)**: Fast steady-state kinematic checks for millions of unit/property tests.
- **Level 2 (Dynamic Plant - Recommended Sweet Spot)**: First-order motor lag, vehicle mass, drag, grade, rolling resistance, encoder quantization, and discrete CAN delays.
- **Level 3 (High-Fidelity Vehicle Dynamics - Future)**: Non-linear tire slip models, Pacejka curves, multi-cell battery electrochemical models, suspension pitch dynamics, and hydraulic pressure curves.

---

### 8.16 Primary Baseline Integration Test

The foundational physics test that must run clean before hardware integration:

```text
Given:
  Initial speed = 0 mm/s
  Road grade = 0°
  Nominal mass, battery, and tire radius

When:
  Host commands 3000 mm/s via 0x300

Then:
  - Host frame encodes cleanly
  - RT receives and validates 0x300
  - RT generates valid 0x204 RT_DRIVE_CMD
  - MTR accepts 0x204 and validates state
  - DAC voltage ramps within slew and current limits
  - Motor torque increases smoothly
  - Wheel tractive force accelerates vehicle plant
  - Encoder model registers pulse increments
  - Frame 0x206 reports feedback speed
  - RT PID tracks setpoint and settles near 3000 mm/s

And:
  - No command exceeds physical DAC/current limits
  - Zero NaN / Inf values
  - No directional relay conflicts
  - Zero oscillatory or limit-cycle instability
  - Measured acceleration remains bounded below physical ceiling
```

Re-run this test across the complete tolerance envelope ($\pm 15\%$ mass, $\pm 10\%$ motor gain, $\pm 20\%$ time constant, $\pm 2\%$ tire radius, $\pm 10\%$ efficiency, $\pm 30\%$ rolling resistance, CAN latency jitter, and quantization noise) to prove end-to-end stability from **top-level command to physical ground contact**.

---

## 9. Vehicle Subsystem Closed-Loop Suites

Beyond longitudinal propulsion, full pre-hardware confidence requires closed-loop software-in-the-loop simulation of all vehicle subsystems: lateral steering (SES), mechanical braking (SEB), dual-bus routing (RT Gateway), system mode transitions, and sensor plausibility checking.

### 9.1 Lateral / Steering Dynamics & Closed-Loop Actuation (SES SIL)

Longitudinal dynamics alone cannot validate vehicle trajectory stability. The lateral SIL integrates kinematic bicycle models with the Smart Electronic Steering (SES) subsystem.

#### Kinematic Bicycle & Lateral Dynamics Model
At velocity $v$ and steering road-wheel angle $\delta$, with wheelbase $L$:

$$
r = \dot{\psi} = \frac{v}{L} \tan\delta
$$

Lateral acceleration:

$$
a_y = v r = \frac{v^2}{L} \tan\delta
$$

Tire slip angles for front and rear axles ($lpha_f, lpha_r$):

$$
lpha_f = \delta - rctan\left(\frac{v_y + a r}{v_x}
\right), \quad lpha_r = -rctan\left(\frac{v_y - b r}{v_x}
\right)
$$

Where $a$ and $b$ represent distances from the center of gravity to front and rear axles.

#### Closed-Loop SES Signal Chain
```text
HOST PATH PLANNER
       ↓
0x300 STEER_REQ (High CAN)
       ↓
RT Kinematic Validation & Rate Limiting
       ↓
0x169 VCU_SES_REQ (Low CAN: Target Angle, Enable Flag, Slew Rate)
       ↓
SES CONTROLLER MODEL (Position PID, Current Limit, Motor Time Constant)
       ↓
STEERING COLUMN & RACK KINEMATICS
       ↓
ROAD WHEEL STEER ANGLE (δ)
       ↓
0x201 SES_STATUS (Feedback Angle, Actual Current, Fault State)
       ↓
RT & HOST STEERING MONITOR
```

#### SES Verification Invariants
- **Angle Slew Rate Clamping**: Command exceeding maximum physical slew rate (e.g. $> 180^\circ/\text{s}$) is clamped smoothly; no step angle commands reach actuator.
- **Following-Error Watchdog**: If $|\delta_\text{cmd} - \delta_\text{actual}| > \text{Threshold}$ for $> 200\text{ ms}$, RT aborts autonomous mode, reports steering following error DTC, and executes safe stop.
- **Steering Runaway Prevention**: Simulated sensor fault commanding full lock at high speed ($v > 2000\text{ mm/s}$) is intercepted by lateral acceleration supervisory limiter ($a_y \le a_{y,\text{max}}$).

---

### 9.2 Mechanical Brake Priority & Blending (SEB SIL)

The vehicle utilizes a dual braking mechanism: regenerative/motor deceleration and a Smart Electronic Braking (SEB) pneumatic/hydraulic friction brake actuator.

#### Absolute Brake Priority Invariant
```text
BRAKE OVERRIDES THROTTLE INVARIANT:
Whenever mechanical brake pressure > 0 OR 0x205 RT_BRAKE_CMD > 0:
Motor commanded tractive torque MUST BE FORCED TO ZERO (T_cmd = 0).
Propulsion is strictly inhibited until brake is 100% released AND rearm edge occurs.
```

#### Non-Instantaneous Brake Fluid / Actuator Lag
Simulate pressure buildup dynamics:

$$
\tau_b \dot{P}_\text{brake} + P_\text{brake} = K_b u_\text{brake}
$$

$$
F_\text{brake} = P_\text{brake} A_\text{caliper} \mu_\text{pad} \cdot 2 \cdot \frac{r_\text{rotor}}{r_\text{wheel}}
$$

Validate:
- Brake rise time from electrical command trigger to 90% clamp force ($t_\text{rise} \le 150\text{ ms}$).
- Residual drag check: Zero residual brake clamp when command returns to 0.

#### SEB Communication Fault Injection
- Drop SEB status frame `0x201` for $> 100\text{ ms}$ $	o$ System detects timeout $	o$ Asserts degraded stop or mechanical spring-applied brake failsafe.
- Corrupt SEB error report `0x202` $	o$ Decoders remain stable without system panic.

---

### 9.3 Dual-Bus Gateway Routing & Schedulability (RT Gateway)

The RT-ESP32 acts as a bridge between the High Bus (MCP2515 via SPI / Host connection) and the Low Bus (Internal TWAI / Powertrain).

#### Dual-Bus Isolation & Routing Matrix
```text
                  ┌───────────────────────────────┐
                  │          RT GATEWAY           │
HIGH BUS (500k) ──┤ MCP2515 SPI ──→ ROUTE TABLE   ├── LOW BUS (500k)
(Host, Telemetry) │                 TWAI Controller │ (MTR, SES, SEB, SYS)
                  └───────────────────────────────┘
```

Verify the routing contract under simulation:
1. **Routable Message Passthrough**:
   - `0x001` (ESTOP): Bidirectional single-hop bridge (High $\leftrightarrow$ Low).
   - `0x110` (Mode Command): Bridged strictly according to configuration.
   - `0x204` (Drive Command): Low bus only; never forwarded onto High bus.
   - `0x300` (Host Drive Request): High bus only; decoded by RT, never echoed to Low bus directly.
2. **Leakage Prevention**:
   - Inject unmapped CAN IDs on High bus; assert zero leakage onto Low bus.
   - Inject private powertrain CAN IDs on Low bus; assert zero leakage onto High bus.
3. **SPI Transceiver Latency & FIFO Saturation Under Load**:
   - MCP2515 SPI burst transfer requires $pprox 80\ \mu\text{s}$ per 13-byte CAN frame.
   - Subject High bus to 90% bus load burst; assert RT TWAI low-bus 100 Hz drive loop (`0x204`) executes without deadline jitter ($< 1\text{ ms}$ jitter permitted).
   - Verify ring-buffer saturation drop policy: High telemetry drops newest; safety and control frames are never dropped.

---

### 9.4 Mode Management & Operator Takeover Logic

Vehicle state transitions governed by `0x110 SYS_MODE_CMD` must be deterministically proven in SIL:

```text
       ┌──────────────┐   Startup / Self-Test Clean
       │  SAFE_IDLE   ├─────────────────────────────┐
       └──────┬───────┘                             │
              │ Operator Switch                     ▼
              ▼                             ┌──────────────┐
       ┌──────────────┐                     │  CALIBRATION │
       │    MANUAL    │                     └──────────────┘
       └──────┬───────┘
              │ Standstill + Zero Throttle + Host Heartbeat Valid
              ▼
       ┌──────────────┐
       │  AUTONOMOUS  │
       └──────┬───────┘
              │ Physical Driver Intervention (Brake / Steer)
              ▼
       ┌──────────────┐
       │   OVERRIDE   ├─────────────────────┐
       └──────────────┘                     │
              ▲                             │
              │ ESTOP Triggered Anywhere    │
       ┌──────┴───────┐                     │
       │    ESTOP     │◄────────────────────┘
       └──────────────┘
```

#### Verification Rules
- **Prohibited Autonomous Entry**: Attempt `MANUAL` $	o$ `AUTONOMOUS` transition while vehicle speed $> 0$ or throttle $> 0$. Expected: Transition rejected; mode remains `MANUAL`.
- **Seamless Operator Takeover**: While driving in `AUTONOMOUS` at 2500 mm/s:
  - Driver touches physical brake pedal ($> 5\%$ travel) $	o$ Instant transition to `OVERRIDE` / `MANUAL` ($< 20\text{ ms}$).
  - Driver applies manual steering torque exceeding threshold $	o$ Autonomous steering disengages immediately without kickback.
- **Autonomous Trajectory Lockout**: Inject autonomous trajectory frames (`0x300`) while in `MANUAL` mode $	o$ Frames ignored; zero motor torque produced.

---

### 9.5 Sensor Plausibility & Fault Trapping

Before sensor values are consumed by control tasks, SIL simulation validates input conditioning and fault detectors:

#### Dual-Channel Throttle & Brake ADC Plausibility
- SYS samples Potentiometer 1 ($V_1$) and Potentiometer 2 ($V_2$):
  $$
  |V_1 - f(V_2)| \le \Delta V_\text{tolerance} \quad (10\%)
  $$
- Fault Injections:
  - Disconnect Sensor 1 (pull-down / $0\text{ V}$) $	o$ Plausibility error asserted within 20 ms $	o$ Throttle forced to zero $	o$ DTC raised.
  - Short Sensor 2 to 3.3V/5V $	o$ Out-of-range clamp triggered $	o$ Throttle inhibited $	o$ DTC raised.
  - Skew $V_1$ vs. $V_2$ by $15\%$ $	o$ Inhibit drive; transition to safe state.

#### Ground Speed vs. Motor Encoder Cross-Check
- Compare ground wheel speed derived from wheel sensors vs. motor shaft speed:
  $$
  |v_\text{wheel} - \frac{\omega_\text{motor} r_w}{G}| \le \Delta v_\text{slip}
  $$
- Detects broken drivetrain coupling, sheared keyway, slipping belt, or locked brakes.

---

## 10. Concrete Test Harness & Code Architecture

To execute the verification plan, test code is organized into a modular native and simulation hierarchy:

### 10.1 Repository Test Directory Blueprint

```text
etrike/
├── native-test/                       # C++ Host Unit, Fuzzing & Component Suites
│   ├── CMakeLists.txt                 # Native CMake with ASan, UBSan, TSan targets
│   ├── FreeRTOSConfig.h               # Native FreeRTOS Simulator configuration
│   ├── sim-engine/                    # FreeRTOS multi-tasking host execution engine
│   │   ├── sim_time.cpp               # Deterministic virtual timekeeper
│   │   └── virtual_twai.cpp           # Loopback TWAI/MCP2515 CAN driver
│   ├── test/                          # C++ test binaries
│   │   ├── test_diagnostic_manager.cpp # WP1 exhaustive state-machine tests
│   │   ├── test_estop_suite.cpp       # 22 ESTOP scenario tests
│   │   ├── test_gateway_routing.cpp   # RT dual-bus isolation & forwarding
│   │   ├── test_can_decoders_fuzz.cpp # libFuzzer CAN protocol entrypoints
│   │   └── test_heap_allocation.cpp   # Malloc/new interception harness
│   └── scripts/                       # Native build and test orchestration scripts
│
├── simulation/                        # Closed-Loop SIL & Python Verification
│   ├── sil/
│   │   ├── bus/
│   │   │   ├── virtual_can_bus.py     # Deterministic dual-bus software CAN router
│   │   │   └── can_frame.py           # Struct packing/unpacking mirrors
│   │   ├── models/
│   │   │   ├── vehicle_plant.py       # Longitudinal + lateral kinematic bicycle plant
│   │   │   ├── motor_model.py         # Torque/power limited motor with thermal lag
│   │   │   ├── steering_model.py      # SES steering column and rack actuator
│   │   │   ├── brake_model.py         # Hydraulic pressure buildup and pad friction
│   │   │   └── encoder_model.py       # Pulse generation, quantization, and noise
│   │   ├── ref_models/
│   │   │   └── model_diag.py          # Pure-Python DiagnosticManager reference model
│   │   ├── runners/
│   │   │   ├── run_monte_carlo.py     # 10,000-run parameter variation harness
│   │   │   └── run_sil_fault_suite.py # End-to-end multi-node fault injection runner
│   │   └── tests/
│   │       ├── test_differential_diag.py # Pytest + Hypothesis differential tests
│   │       ├── test_closed_loop_drive.py # 3000 mm/s step and PID stability tests
│   │       ├── test_ses_steering_loop.py # Lateral tracking and following-error tests
│   │       ├── test_estop_physics.py     # Stopping time and distance SIL tests
│   │       └── test_stale_authority.py   # Authority resurrection and replay tests
```

---

### 10.2 Memory Allocation Interception Harness

A dedicated C++ memory trap guarantees zero heap allocation after initialization:

```cpp
// native-test/test/test_heap_allocation.cpp
#include <atomic>
#include <cstdlib>
#include <new>
#include <cassert>

static std::atomic<bool> g_trap_allocations{false};
static std::atomic<size_t> g_allocation_count{0};

void* operator new(std::size_t size) {
    if (g_trap_allocations.load()) {
        g_allocation_count.fetch_add(1);
    }
    return std::malloc(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

// In test execution:
void run_zero_allocation_verification() {
    DiagnosticManager mgr;
    mgr.init();
    
    // Arm allocation trap
    g_trap_allocations.store(true);
    g_allocation_count.store(0);
    
    // Execute 1,000,000 representative lifecycle operations
    for (int i = 0; i < 1000000; ++i) {
        mgr.raise(DIAG_MTR_CAN_TIMEOUT, 0x1234);
        mgr.pop_pending_report();
        mgr.recover(DIAG_MTR_CAN_TIMEOUT);
        mgr.clear(DIAG_MTR_CAN_TIMEOUT);
    }
    
    // Disarm trap & assert
    g_trap_allocations.store(false);
    assert(g_allocation_count.load() == 0); // Must be strictly zero
}
```

---

### 10.3 libFuzzer Target Implementation

Fuzzing targets consume LLVM coverage-guided byte streams:

```cpp
// native-test/test/test_can_decoders_fuzz.cpp
#include <cstdint>
#include <cstddef>
#include "protocol/generated/can_decoders.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 4) return 0;
    
    // Split bytes into CAN ID, DLC, and payload
    uint16_t can_id = (data[0] << 8) | data[1];
    uint8_t dlc = data[2] % 16;
    const uint8_t *payload = (size >= 4 + dlc) ? &data[3] : nullptr;
    
    if (!payload) return 0;
    
    // Feed into decoders
    CanFrame frame{can_id, dlc, payload};
    sys_decode_frame(frame);
    rt_decode_frame(frame);
    mtr_decode_frame(frame);
    
    return 0; // Return 0 indicates non-crash execution
}
```

---

## 11. CI/CD Pipeline & Automated Test Runner Infrastructure

The verification stack runs within automated continuous integration pipelines to prevent regressions:

### 11.1 Continuous Integration Matrix

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                           GITHUB ACTIONS / CI WORKFLOWS                     │
├───────────────────┬───────────────────┬───────────────────┬─────────────────┤
│   Native Tests    │     Sanitizers    │   Cross-Compile   │    SIL Physics  │
│ (GCC & Clang 17)  │(ASan, UBSan, TSan)│ (SYS, RT, MTR)    │(Pytest + Hypoth)│
├───────────────────┼───────────────────┼───────────────────┼─────────────────┤
│ • ctest -j8       │ • cmake -DSAN=ON  │ • idf.py build SYS│ • pytest sil/   │
│ • -Werror clean   │ • 0 findings      │ • idf.py build RT │ • 10k MonteCarlo│
│ • Branch cov 100% │ • 0 TSan races    │ • arm-none-eabi   │ • Envelopes PASS│
└───────────────────┴───────────────────┴───────────────────┴─────────────────┘
```

### 11.2 Standard Execution Commands

#### Universal Master Test Runner (Recommended)
Run all native, Python SIL, and TypeScript protocol suites in one unified command:

```bash
# Run everything (C++, Python SIL, and Simulation):
python scripts/run_all_tests.py
# Or via npm from repository root:
npm test

# Windows shortcut batch file:
.\scripts\test.cmd

# PowerShell shortcut:
.\scripts\test.ps1

# Subsystem-specific flags:
python scripts/run_all_tests.py --native    # Run only native C++ suites (Exhaustive, Heap, ESTOP)
python scripts/run_all_tests.py --python    # Run only Python SIL suites (Reference model, Physics)
python scripts/run_all_tests.py --sim       # Run only TypeScript simulation suites
python scripts/run_all_tests.py --pio       # Run PlatformIO native test pipelines (SYS, RT, MTR)
python scripts/run_all_tests.py --quick     # Fast subset run
```

#### PlatformIO Native Firmware Test Pipelines
PlatformIO native tests compile and run production firmware components on host architectures with mocked HAL drivers:

```bash
# 1. SYS-ESP32 Host Native Test Suite (31 tests across 8 suites)
pio test -d sys-esp32 -e native

# 2. RT-ESP32 Host Native Subsystem Tests (55 tests across 13 suites)
pio test -d rt-esp32 -e native

# 3. RT-ESP32 End-to-End Pipeline Math Suite (61 tests across 14 suites)
# Chains RT PhysicsModel setpoints into MTR MotorManager DAC code vectors
pio test -d rt-esp32 -e native_pipeline

# 4. MTR-STM32 Complete Subsystem & DAC Golden Vector Suite (15 suites / 170 assertions)
pio test -d mtr-stm32 -e native
```

> **Note on Windows Host Toolchains**:
> Default MinGW GCC 6.3.0 distributions lack C++17 `<string_view>`. Ensure a modern C++17/20 toolchain (e.g. LLVM-MinGW `clang++`/`g++`) is first on `PATH` before invoking `pio test -e native`, or use `python run_all_tests.py --pio` which automatically configures the toolchain environment.

#### Native C++ Suite Manual Build & Run
```bash
mkdir native-test/build && cd native-test/build
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release ..
ninja
ctest --output-on-failure -j8
```

#### Native Sanitizer Execution (ASan + UBSan)
```bash
mkdir native-test/build-san && cd native-test/build-san
cmake -G "Ninja" -DENABLE_SANITIZERS=ON ..
ninja
ctest --output-on-failure
```

#### ThreadSanitizer Concurrency Stress
```bash
mkdir native-test/build-tsan && cd native-test/build-tsan
cmake -G "Ninja" -DENABLE_TSAN=ON ..
ninja
./test_concurrency_stress
```

#### Python SIL Closed-Loop & Hypothesis Tests
```bash
pytest simulation/sil/tests/ -v -n auto
python simulation/sil/runners/run_monte_carlo.py --runs 10000
```

#### Target Cross-Compilations
```bash
# SYS ESP32-S3
idf.py -C sys-esp32 build

# RT ESP32-S3
idf.py -C rt-esp32 build

# MTR STM32G431
make -C mtr-stm32 -j8
```

---

## 12. Phased Build-Out & Implementation Roadmap

To systematically build the testing architecture, execute the six defined work packages in sequence:

```text
Phase 1: Architecture & Model Definition
  ├── Deliverable: Python Reference Model (model_diag.py)
  └── Deliverable: Deterministic Virtual Dual CAN Bus (virtual_can_bus.py)

Phase 2: WP1 Core Logic & Exhaustive Unit Testing
  ├── Deliverable: Exhaustive C++ DiagnosticManager tests (100% branch coverage)
  ├── Deliverable: Hypothesis differential testing harness (test_differential_diag.py)
  └── Deliverable: Mutation testing suite (>= 90% kill rate)

Phase 3: Sanitizers, Concurrency & Low-Level Budgets
  ├── Deliverable: ASan + UBSan native test runs (0 findings)
  ├── Deliverable: Explicit task ownership or TSan stress suite (0 races)
  ├── Deliverable: Malloc/new allocation trap test (0 heap allocations)
  └── Deliverable: Target stack usage audit (-fstack-usage inspection)

Phase 4: Dedicated ESTOP & Safety Reset Suite
  ├── Deliverable: 22 explicit ESTOP scenario tests implemented in C++ & Python
  ├── Deliverable: Property-based test suite verifying global safety invariants P1–P8
  └── Deliverable: Asymmetric 0x011 multi-frame clear authorization engine

Phase 5: Subsystems & Closed-Loop Physics SIL
  ├── Deliverable: Longitudinal dynamic plant (F_drive, drag, grade, motor lag, torque limits)
  ├── Deliverable: Lateral kinematic bicycle & SES steering closed loop (0x169 -> 0x201)
  ├── Deliverable: Mechanical brake priority & fluid buildup model (SEB)
  ├── Deliverable: RT dual-bus gateway routing & FIFO saturation harness
  └── Deliverable: 10,000-run Monte Carlo parameter uncertainty stability suite

Phase 6: Multi-Compiler & Target Cross-Build Verification
  ├── Deliverable: Clean GCC & Clang host builds with -Werror
  ├── Deliverable: Clean ESP32-S3 SYS build (ESP-IDF)
  ├── Deliverable: Clean ESP32-S3 RT build (ESP-IDF)
  └── Deliverable: Clean STM32G431 build (arm-none-eabi)
```

---

## 13. Pre-Hardware Release Gate Checklist

No firmware may be flashed to hardware and no physical actuators may be connected until **every single item below passes cleanly**:

```text
Core Logic & Differential Verification:
[ ] Standard pytest suite passes
[ ] Exhaustive WP1 state-transition tests pass (100% branch/transition coverage)
[ ] Python reference-model differential tests pass with zero discrepancies
[ ] Property-based randomized sequence tests pass (Hypothesis)
[ ] WP1 mutation score validated (>= 90% mutation kill rate)

Memory, Concurrency & Resource Hardening:
[ ] Clean AddressSanitizer (ASan) run (0 findings)
[ ] Clean UndefinedBehaviorSanitizer (UBSan) run (0 findings)
[ ] Clean ThreadSanitizer (TSan) run OR single-owner task queue verified
[ ] Zero runtime heap allocation confirmed via malloc/new interception trap
[ ] Target worst-case stack frames audited via -fstack-usage within budget
[ ] Simulated tick-wraparound tests pass (49.7 day rollover)

Protocol, Decoders & Fuzzing:
[ ] CAN decoder libFuzzer suite passes clean (zero faults / panics)
[ ] DiagnosticManager libFuzzer suite passes clean
[ ] All-42-diagnostic saturation test passes without starvation or array overflow
[ ] Diagnostic report queue overflow behavior validated under maximum load
[ ] Cross-language protocol hash and vector roundtrip tests pass (C++, TS, Python)

ESTOP & Safety Reset Suite:
[ ] All 22 dedicated ESTOP scenario tests pass
[ ] All 8 global safety invariants (P1–P8) pass randomized SIL testing
[ ] Asymmetric 0x011 multi-frame clear authorization rule verified
[ ] Assert dominance over clear strictly proven
[ ] Reboots during active ESTOP proven safe across all ECU combinations
[ ] Pre-ESTOP authority resurrection strictly prevented (stale 0x110, 0x113, 0x204 rejected)
[ ] Gateway 0x001 bridge echo loop prevention verified

Vehicle Subsystems & Closed-Loop Physics SIL:
[ ] Primary closed-loop longitudinal step test passes (3000 mm/s target)
[ ] Motor torque-limit and power-limit curves validated
[ ] Direction reversal sequence safety verified (decelerate → zero → dwell → relay switch)
[ ] Actuator saturation & anti-windup desaturation verified (< 500 ms recovery)
[ ] Absolute brake priority over throttle verified in software
[ ] Lateral SES steering loop tracking and slew rate limiting verified (0x169 -> 0x201)
[ ] Steering following-error watchdog and runaway protection verified
[ ] RT dual-bus gateway routing isolation and non-leakage proven
[ ] Operator takeover seamless disengagement verified
[ ] Dual-channel ADC sensor plausibility and fault traps verified
[ ] ESTOP physical stopping distance and reaction time verified within calculated margins
[ ] Monte Carlo parameter sweeps pass stability criteria across 10,000 randomized plant profiles

Multi-Compiler & Target Cross-Compilation:
[ ] Bit-for-bit non-interference verified (diagnostics-on vs. diagnostics-off)
[ ] Clean GCC C++17 build with -Werror
[ ] Clean Clang C++17 build with -Werror
[ ] Clean static analysis (clang-tidy and cppcheck)
[ ] ESP32-S3 SYS firmware cross-compiles cleanly (ESP-IDF)
[ ] ESP32-S3 RT firmware cross-compiles cleanly (ESP-IDF)
[ ] STM32G431 firmware cross-compiles cleanly (arm-none-eabi)
[ ] Code-generated artifacts verified clean and synchronized
```

### Final Signoff Protocol
Upon achieving green status across all 35 checklist gates, the software gate is declared **PASSED**. Firmware may then proceed to bench testing on isolated ECUs prior to actuator harness connection.


No firmware may be flashed to hardware and no actuators may be connected until all items below pass cleanly:

```text
[ ] Standard pytest suite passes
[ ] Dedicated ESTOP & safety reset test suite passes all explicit scenario tests
[ ] Primary closed-loop physics SIL test passes (3000 mm/s step response)
[ ] Closed-loop PID robustness verified across ±15% mass and ±20% motor lag perturbations
[ ] Direction reversal sequence safety verified (decelerate → zero → dwell → relay switch)
[ ] Actuator saturation & anti-windup desaturation verified (< 500 ms recovery)
[ ] ESTOP physical stopping distance and reaction time verified within calculated margins
[ ] Monte Carlo parameter sweeps pass stability criteria across 10,000 randomized plant profiles
[ ] All 8 global safety invariants (P1-P8) pass randomized SIL testing
[ ] Native C++ unit tests pass
[ ] Exhaustive state-transition tests pass (100% branch/transition coverage)
[ ] Python reference-model differential tests pass
[ ] Property-based randomized sequence tests pass (Hypothesis)
[ ] Clean AddressSanitizer (ASan) run (0 findings)
[ ] Clean UndefinedBehaviorSanitizer (UBSan) run (0 findings)
[ ] Clean ThreadSanitizer (TSan) run OR single-owner concurrency model verified
[ ] CAN decoder fuzzing suite passes clean
[ ] DiagnosticManager libFuzzer suite passes clean
[ ] WP1 mutation score strong (>= 90% kill rate)
[ ] Zero runtime heap allocation confirmed via memory hooks
[ ] Simulated tick-wraparound tests pass
[ ] All-42-diagnostic saturation test passes
[ ] Diagnostic report queue overflow behavior validated under load
[ ] Bit-for-bit non-interference verified (diagnostics-on vs. diagnostics-off)
[ ] Full vehicle SIL fault-injection suite passes
[ ] Clean GCC C++17 build with -Werror
[ ] Clean Clang C++17 build with -Werror
[ ] Clean static analysis (clang-tidy and cppcheck)
[ ] ESP32-S3 SYS target cross-build passes
[ ] ESP32-S3 RT target cross-build passes
[ ] STM32G431 target cross-build passes
[ ] Code-generated artifacts verified clean and synchronized
[ ] Cross-language protocol hash and vector roundtrip tests pass
```

### Conclusion & Priority for WP1
While no automated suite can mathematically prove the absolute absence of software faults, this verification stack eliminates the overwhelming majority of logic, memory safety, protocol synchronization, concurrency, timing, and integration defects **before physical hardware enters the loop**.

For WP1 specifically, prioritize:
1. **Python reference-model differential testing**
2. **Property-based randomized operation sequences**
3. **ASan + UBSan native suites**
4. **Explicit concurrency definition & TSan validation**
5. **Diagnostic non-interference differential testing**
