# Codebase Architecture and YAML Gap Audit

- **Audit date:** 2026-07-13
- **Document ownership:** repository-wide architecture and integration
- **Scope:** RT, SYS, MTR, PWT, host integration, the CAN contract, generated artifacts, existing debug/simulation tools, and codebase tests.
- **Purpose:** identify where the documented architecture, YAML CAN definitions, generated artifacts, and executable code disagree or remain incomplete.
- **Remediation plan:** [`codebase-remediation-plan.md`](codebase-remediation-plan.md)

This is a codebase traceability and test-readiness document. It does not specify or implement the new Control UI. UI consequences are noted only where a codebase mismatch would make any downstream diagnostic consumer unreliable.

## 1. Audit result

> **Remediation update (2026-07-14):** The original findings below remain as
> traceability records. CAN IDs/DLCs/routes and protocol hashes are now generated
> and consumed by production interfaces. Checked typed codecs now cover ordinary
> RT/SYS/MTR/Jetson messages; registered SES/SEB adapters cover vendor checksum
> and overlay behavior. Per-message hashes, change-impact metadata, mapping review,
> and unregistered-wire-access checks are enforced by CI. The `0x600`, `0x7FE`, `0x7FC`, `0x210`,
> HMI mode, forwarding, build-profile, and bounded RT event-queue mismatches have
> been corrected. PWT is now explicitly standalone, owns generated
> `0x10262B27`, and the nonexistent low-bus `0x012` route is retired. Native
> software verification can therefore cover RT/SYS protocol behaviour. Full
> RT/SYS/MTR hardware conformance remains open because MTR board initialization,
> direct ESTOP wiring, and hardware-in-loop evidence cannot be completed in
> repository-only remediation.

The current repository is **not yet contract-consistent enough to support a conclusive end-to-end RT/SYS/MTR conformance result**. Raw CAN capture and many individual tests remain useful, but the following blockers can make a correct diagnostic consumer report incorrect values or make an apparently passing test prove the wrong implementation:

1. Generated IDs, DLCs, routes, protocol hashes, metadata and ordinary payload codecs are compiled by production interfaces. Legacy compatibility wrappers remain pending retirement; registered vendor adapters remain intentionally handwritten and tested.
2. Live `0x600`, `0x7FE`, `0x7FC`, and `0x210` definitions now agree with the canonical YAML and generated consumers.
3. Vehicle builds default to production mode; explicit hardware-bench and software-bench profiles select their intended bypass level.
4. SYS now reports task freshness bits and transition-based diagnostics, but hardware reset/NVS evidence remains bench work.
5. MTR remains a hardware-incomplete scaffold and is explicitly unavailable for vehicle actuation.
6. PWT is a supported standalone powertrain node, not a gateway; a future gateway still requires different or additional CAN hardware.

`python shared/can/generate_code.py --verify` passes. That proves the committed generated files match the YAML input; it does **not** prove the firmware, host bridge, simulators, or documentation implement that contract.

### Severity and status

| Severity | Meaning |
|---|---|
| Blocker | Can invalidate an end-to-end bench result or makes the described component unavailable. |
| High | Can cause a wrong decode, missed fault, false connection state, or wrong actuation/test decision. |
| Medium | Causes misleading timing, diagnostics, recovery, or test coverage. |
| Documentation | Implementation may be valid, but the written claim is stale or ambiguous. |

Rows below preserve the evidence captured during the original audit. They are not all statements about the current tree. Consult the status register before treating a row as open; “Required resolution” remains useful target design even when only part of it has been implemented.

### Remediation status register

| Findings | Current status |
|---|---|
| CAN-001, CAN-004, CAN-005, CAN-006 | **Partially resolved:** generated codecs/hashes/bus instances/counter metadata exist; legacy retirement, on-bus hash exposure, complete topology semantics and non-heartbeat counter policy remain open. |
| CAN-002, CAN-003, CAN-007, CAN-008 | **Resolved in current tree:** routes are YAML-derived, cross-bus semantic conflicts fail generation, output is deterministic, and CI verifies it. |
| FRM-002, FRM-003, FRM-004, FRM-005, FRM-006 | **Resolved in current tree:** YAML, generated codecs and migrated consumers agree. |
| FRM-007 | **Open:** vendor version interpretation still requires confirmation and a definitive vector. |
| FRM-008 | **Resolved topology; partially resolved tooling:** PWT is standalone and its manufacturer codec is generated from its own YAML; integration into the shared normalized model remains optional target work. |
| MTR-004, TST-001, TST-007 | **Resolved or superseded:** production targets and Jetson consume generated codecs, and current native/schema vectors cover the corrected layouts. |
| TST-004 | **Partially resolved:** DLC assertions consume generated definitions, while the forwarding test still contains a hand-copied expected route list and should be generated or data-driven. |
| PWT-003 | **Partially resolved:** deterministic PWT generation exists, but it remains a deliberately separate manufacturer contract. |
| Other findings | **Open unless a later row or referenced test demonstrates closure.** Hardware/HIL findings cannot be closed by generation or native tests. |

## 2. Source-of-truth chain

### 2.1 Target chain

```text
can_high.yaml + can_low.yaml
            |
      schema validation
            |
 normalized protocol model
   |          |           |          |
firmware   tool codecs  golden tests  docs
 codecs    and metadata   and traces
```

The normalized model should define stable wire facts. Algorithms should be generated where reliable; otherwise they must be registered, localized, hashed and independently tested manual adapters. Firmware and host code consume one of those two explicit paths. CI rejects unregistered divergence.

### 2.2 Current implemented chain

```text
YAML --> schema/generate_code.py --> generated C++ checked codecs --> RT / SYS / MTR / Jetson
                                +--> TypeScript catalogs
                                +--> codec_manifest.json
                                +--> per-message hashes + change_impact.json

manual-mappings.yaml --> registered SES/SEB adapter --> RT / SYS
                      --> reviewed hashes, consumers, tests and builds

PWT manufacturer YAML --> deterministic extended-frame PWT codec

can_protocol.h --> legacy compatibility structs and common frame/enums (retirement pending)
manual schedules/behaviour --> debug simulator (target: consume generated periods/policy)
```

The former untracked split is closed for ordinary application messages and explicitly controlled for registered vendor exceptions. Remaining risk is concentrated in legacy compatibility use, simulator behavior, incomplete algorithm metadata and hardware/HIL evidence rather than hidden payload ownership.

## 3. CAN contract and generation gaps

| ID | Severity | Gap and evidence | Consequence | Required resolution |
|---|---|---|---|---|
| CAN-001 | Blocker | [`generate_code.py`](shared/can/generate_code.py) emits [`generated/can_data.h`](shared/can/generated/can_data.h), but RT, SYS, and MTR include [`can_protocol.h`](shared/can/can_protocol.h). The generated header appears in generator tests, not production firmware. RT/SYS pre-build hooks regenerate an artifact they do not compile; MTR has no equivalent hook. | YAML verification can pass while firmware bytes differ. Automated tests may validate the catalog rather than the flashed contract. | Generate production codecs/layouts and make all firmware include them. Keep hand-written control logic outside the generated file. Add CI that fails if production targets do not consume the generated contract. |
| CAN-002 | High | Forwarding arrays in [`generate_code.py`](shared/can/generate_code.py), the copied native forwarding test, and architecture tables are hand-maintained. They omit HMI `0x111`/`0x112`, while [`can_protocol.h`](shared/can/can_protocol.h) and RT routing forward them. | Generated/existing tools and tests describe a different route from firmware. | Put route metadata in canonical YAML and generate runtime routing, documentation, and tests from it. |
| CAN-003 | High | Duplicate IDs across the two YAML files are checked mainly for DLC. The generator keeps the first signal definition and does not require complete equality of names, signals, enums, timing, or comments. `0x206` already differs: high YAML documents `STARTUP_READY`; low YAML does not. | A conflicting contract can generate successfully, with bus-dependent or load-order-dependent meaning. | Validate a canonical message definition once, then reference it from bus routes. Until migrated, compare the complete normalized definition for duplicate IDs and fail generation on any semantic difference. |
| CAN-004 | High | Neither YAML nor firmware exposes a protocol semantic version/hash over CAN. | No diagnostic consumer can prove that a flashed ECU matches the catalog used to decode it. | Generate a protocol hash from normalized YAML, embed it in every firmware image, and expose it through a version/diagnostic frame or diagnostic request. Record it in every test session. |
| CAN-005 | Medium | Forwarded messages are duplicated by bus and receiver lists do not consistently describe physical topology. There is no explicit `origin_bus`, route, or “independent instance” attribute. | Forwarded `0x206`/`0x600` may be double-counted, while RT's two independent `0x7FD` heartbeats could be incorrectly deduplicated. | Model one message definition plus bus routes. Add `origin`, `forwarded_via`, and `instance_semantics` (`same_frame` or `independent`). |
| CAN-006 | Medium | Counter semantics are absent. RT overflow wraps after an internal `uint16_t` is cast to a byte; SYS overflow is a six-bit saturating field; heartbeat counters wrap modulo 256. | A wrap can look like recovery, and a saturated counter can look frozen. | Add `counter_kind`, bit width/modulus, saturation, reset scope, and expected increment rules to YAML. |
| CAN-007 | Medium | The generated forwarding lists are labelled “hand-maintained, matches can_protocol.h”, making two purported authorities. | Drift is expected and cannot be resolved automatically. | Remove hand-maintained protocol facts from the generator; the generator should transform data, not contain protocol data. |
| CAN-008 | Medium | Generated files contain wall-clock timestamps. `generate_code.py --verify` is read-only but deliberately ignores those lines, while RT/SYS PlatformIO pre-build hooks run the generator in write mode and rewrite them on every build. | Builds can dirty the working tree, generated output is not reproducible byte-for-byte, and verification cannot assert exact artifact identity. | Remove wall-clock timestamps from generated content or derive stable metadata separately; make verification compare deterministic output byte-for-byte, and make ordinary builds check rather than rewrite committed artifacts. |

## 4. Frame-level mismatches

| ID | Severity | Contract/document claim | Actual code | Consequence and resolution |
|---|---|---|---|---|
| FRM-001 | High | [`can_high.yaml`](shared/can/can_high.yaml) says RT state `0x210` is also sent on low, but [`can_low.yaml`](shared/can/can_low.yaml) has no definition for it. | [`rt-esp32/src/main.cpp`](rt-esp32/src/main.cpp) transmits `0x210` on both high and low; SYS consumes the low copy. | A low-bus monitor sees an unknown/wrong-bus frame. Define one canonical `0x210` message and both transmit routes. |
| FRM-002 | High | Low YAML defines `0x7FE` byte 1 as heartbeat bit 0, ESTOP bit 1, bits 2–3 reserved. | SYS sets bit 2 `mode_auto` and bit 3 `can_ok`, matching the root architecture rather than YAML. | A generated decoder loses two health indicators or reports reserved-bit corruption. Update YAML and generate named bit masks/codecs. |
| FRM-003 | High | Both YAML files define `SYS_DiagHeartbeatOk` as all eight bits of byte 2 and omit RX overflow. | [`can_protocol.h`](shared/can/can_protocol.h) packs heartbeat OK in byte 2 bit 0 and a six-bit, saturating `rx_overflow` in bits 1–6; SYS transmits it. | Every nonzero overflow corrupts a YAML-based decoder's apparent heartbeat value. Correct the YAML to bit 0 plus bits 1–6, reserve bit 7, and add counter semantics. |
| FRM-004 | High | `0x7FC HOST_HEARTBEAT` is DLC 2 in YAML and the shared protocol: alive counter plus health flags. | [`vehicle_bridge_node.cpp`](jetson/src/autoware_vehicle_bridge/src/vehicle_bridge_node.cpp) sends DLC 1. RT currently reads only byte 0. | The system can appear alive while strict validation correctly flags every heartbeat malformed. Generate the host codec and send DLC 2; do not relax contract validation to DLC 1. |
| FRM-005 | High | HMI `0x111` mode values are `0=MANUAL`, `1=AUTO`, `2=PURE_SIM`; the shared global `Mode` uses value 2 for ESTOP. | SYS explicitly accepts only values 0 and 1. | A generated control surface may offer `PURE_SIM`, which SYS rejects, and another decoder may label 2 as ESTOP. Use distinct enums for requested operating mode and reported safety/mode state; either implement value 2 or remove it from the wire contract. |
| FRM-006 | Medium | High YAML documents `0x206` byte 2 bit 4 `STARTUP_READY`; low YAML omits it. The field is named `fault_flags`. | MTR sets this non-fault readiness bit. MTR also reuses the ADC fault bit when a DAC write fails. | Readiness is missing on one bus and fault root cause is ambiguous. Make both routes share one layout; separate status from fault bits or rename the byte; allocate a DAC/I2C fault code. |
| FRM-007 | Medium | YAML defines SES version fields as SW byte 0 and HW byte 1 with decimal scaling. | RT diagnostics interpret bytes 0–1 as a SW pair and bytes 2–3 as a HW pair. | Version display/log output disagrees with catalog. Confirm the vendor frame, then fix either YAML or RT parsing and add a golden vector. |
| FRM-008 | High | [`can_low.yaml`](shared/can/can_low.yaml) says PWT bridges `0x012`/`0x001`, while the root and PWT architecture say PWT has only one 250-kbit/s interface and no gateway. The low-bus YAML sends `0x012` toward `DCDC`. | PWT cannot receive that low-bus frame and instead sends the extended `0x10262B27`, DLC 8 manufacturer command directly. | The declared route does not exist. Choose the PWT topology first. If it becomes a gateway, define consumed-and-regenerated translation with timeout/default behaviour; if it remains standalone, remove the low-bus PWT/bridge route and define DCDC ownership accordingly. |

## 5. Timing and liveness gaps

| ID | Severity | Gap | Impact and resolution |
|---|---|---|---|
| TIM-001 | High | [`architecture.md`](architecture.md) lists RT `0x169` at 100 Hz in one table, while YAML and actual RT scheduling are 50 Hz. RT config also contains a 100-Hz constant that the scheduler does not use. | A rate monitor can report healthy firmware as slow or let a simulator mask drift. Make the documented and configured value 50 Hz, or intentionally change implementation and YAML together. Generate rate expectations. |
| TIM-002 | Medium | YAML marks `SYS_MODE_CMD 0x110` as on-change, while SYS also refreshes it every second. | A contract-based monitor may report unsolicited traffic or calculate an incorrect timeout. Model `on_change` plus `refresh_ms: 1000`. |
| TIM-003 | Medium | YAML marks `SYS_DCDC_CMD 0x012` as on-change. SYS evaluates control at 5 Hz but transmits on change plus a five-second refresh; prose currently mixes these rates. | A test can confuse evaluation frequency with bus transmission frequency. Store both control-loop and refresh timing explicitly; frame monitoring should use the transmit contract. |
| TIM-004 | High | SYS records task alive counters, and architecture says a 1-Hz diagnostic task checks four counters, but no check/use of those counters exists. | A dead SYS task can coexist with a live SYS heartbeat, so connection status alone is a false health signal. Implement deadline-based task health and expose bits/counters, or change the architecture to state it is unavailable. |
| TIM-005 | High | SYS marks the SEB rolling counter unhealthy after one duplicate and healthy after one change. SEB status is faster than the command counter, so duplicates can be normal. | Suppression ownership can oscillate and diagnostics may show intermittent corruption where none exists. Evaluate rolling freshness across a time window with expected update period, allowed duplicates, timeout, and recovery hysteresis. |
| TIM-006 | Medium | Debug simulator schedules drift from firmware/YAML: SES and SEB synthetic status is 100 ms rather than 10 ms; MTR is 50 ms rather than 20 ms; the RT model sends some commands at different rates. | Simulator-passing latency/loss tests do not prove real timing behaviour. Generate simulator schedules from YAML and label behavioural models as approximations when their logic is not firmware-equivalent. |

## 6. RT implementation gaps

| ID | Severity | Gap | Impact and resolution |
|---|---|---|---|
| RT-001 | Documentation | Architecture says all RT tasks are pinned to CPU 0. RT creates them with `xTaskCreate`, and the build is not configured as unicore. | Timing assumptions and test expectations are wrong. Either pin intentionally and verify it or document scheduler affinity accurately. |
| RT-002 | High | Architecture says the safety event queue guarantees no missed events. It has depth 16, and the full-queue fallback calls `xQueueOverwrite`; FreeRTOS defines overwrite for queues of length one. | A burst can drop or mishandle state-transition evidence. Use a supported overflow policy, count drops, expose them, and test burst behaviour. |
| RT-003 | Medium | Gateway queue drop counters are incremented locally but neither logged nor transmitted, although architecture says overflow is logged. | Forwarding loss can look like an ECU disconnect on the opposite bus. Export per-direction queue depth/drop totals and a recovery event. |
| RT-004 | Medium | Several active conditions can log at the poll rate: stale commands/task stalls around 10 Hz, CAN-health warnings around 10 Hz, and SES conditions per frame. | Serial logs can flood, obscure causality, and perturb timing. Aggregate by stable error ID and dimensions; log first occurrence, state transition/recovery, and periodic summaries with suppressed counts. |

## 7. SYS implementation gaps

| ID | Severity | Gap | Impact and resolution |
|---|---|---|---|
| SYS-001 | Blocker | [`shared/system_mode.h`](shared/system_mode.h) hard-codes `SYSTEM_RUN_MODE = 2` (Pure Simulation) for all builds. The documented production vehicle profile does not override it. | Hardware heartbeat/sensor bypasses may remain active in what appears to be a vehicle build. Make run mode an explicit build artifact/profile property and emit it in startup/version diagnostics. CI must build and test each profile. |
| SYS-002 | High | `g_brake_fault_active` is set in several paths but never cleared. The architecture does not define it as latched or give a reset rule. | Diagnostics can remain faulted forever after the physical condition clears, with no way to distinguish an intentional latch from a bug. Define latch/reset semantics and expose cause plus first/last timestamps. |
| SYS-003 | High | `can_ok` is described as false in error-passive state, but code treats TEC below bus-off (`<255`) as OK. | An error-passive controller may be shown healthy. Base state on driver status/state, not only a bus-off threshold; expose error-active/passive/bus-off separately. |
| SYS-004 | High | Current SYS bench build defines `TESTING`; physical ESTOP, brake, mode, and light inputs are replaced by fixed values. | The bench profile cannot prove physical body-input wiring. Define separate `simulation`, `CAN bench`, and `hardware bench` profiles with a capability manifest. |
| SYS-005 | Medium | Checksum failures and repeated MTR/SEB conditions can log at frame/poll frequency. SYS internal task state and reset/NVS history are UART-only. | Important events are buried, and external diagnostics cannot inspect internal causes through CAN. Add structured, rate-limited local logs and a documented diagnostic output; do not invent CAN telemetry that firmware does not emit. |

## 8. MTR implementation gaps

MTR is correctly described in the current root architecture as planned/not vehicle-ready. These are implementation-completion gaps, not evidence that a completed MTR has regressed.

| ID | Severity | Gap | Impact and resolution |
|---|---|---|---|
| MTR-001 | Blocker | CubeMX HAL, clock, GPIO, I2C, ADC, and CAN initialization calls are commented or absent; `hcan` has no validated initialization; the direct ESTOP input function is a stub returning false. | No end-to-end MTR hardware test can be considered valid. Complete and validate the board support package and direct ESTOP path before advertising the MTR capability. |
| MTR-002 | High | CAN transmit return values are ignored. | Loss of feedback/heartbeat cannot be attributed to TX failure versus connection loss. Count and expose failed sends, controller state, TEC/REC, bus-off, and recovery. |
| MTR-003 | Medium | `Mcp4725Dac::i2c_failures` is described as consecutive but is not reset on success. | A historical intermittent failure can be interpreted as a current failure streak. Either rename it cumulative or reset it after a successful transaction and expose both streak and total. |
| MTR-004 | High | MTR does not run the YAML generator and consumes the hand-written protocol. | It can drift independently from RT/SYS and generated diagnostic tools. Include it in the generated-contract build and golden-vector tests. |
| MTR-005 | Medium | ADC uses an unbounded `HAL_MAX_DELAY`; current tests cover limited gear/throttle math rather than HAL/CAN/ESTOP paths. | A peripheral fault can block diagnostic progress, and unit tests do not prove hardware behaviour. Use bounded I/O timeouts and add hardware-in-loop acceptance tests. |

## 9. PWT architecture and implementation gaps

The current root and PWT architecture correctly state that PWT has one 250-kbit/s interface and is not a gateway. Other repository artifacts still contain the older gateway assumption, so topology and ownership must be resolved before more PWT firmware is implemented.

| ID | Severity | Gap | Impact and resolution |
|---|---|---|---|
| PWT-001 | Blocker | Root/PWT architecture says PWT is powertrain-only and a future gateway needs new hardware, but the root low-bus map still includes PWT, the root catalog lists low-bus `0x7FB`, low YAML says PWT bridges `0x012`/`0x001`, and source headers still call it a gateway/stub with an obsolete five-task target. | There is no implementable, internally consistent PWT requirement. Choose external controller, dual-CAN MCU, or standalone topology; update architecture, wiring, YAML routes, heartbeat location, and task scope atomically. |
| PWT-002 | High | Current PWT enables and sends the DCDC manufacturer command every 100 ms without an input owner. SYS emits `0x012` on a bus PWT cannot hear. | The repository has two disconnected control concepts. For standalone PWT, define its enable/configuration owner and remove the nonexistent SYS route. For a gateway, implement low-bus receive, freshness timeout, disabled default, translation, and status evidence. |
| PWT-003 | High | PWT uses standalone manufacturer-frame constants and does not consume the shared generated contract. | Extended-ID/DLC/layout changes can drift from documentation and tests regardless of the topology choice. Add the powertrain protocol to the canonical model and generate PWT definitions; remove stale gateway comments and nonexistent behaviour claims. |

## 10. Host, debug tool, and test gaps

| ID | Severity | Gap | Impact and resolution |
|---|---|---|---|
| TST-001 | High | The host bridge hard-codes CAN IDs/layouts instead of consuming generated definitions. | Host/YAML drift caused the existing heartbeat DLC mismatch and can recur. Generate a host-language contract package and golden vectors. |
| TST-002 | Medium | Host emits `0x301` and `0x302` on its 100-Hz control tick when input is available, while YAML describes on-demand/on-change semantics. | Rate and freshness tests disagree with actual traffic. Specify maximum/nominal periodic behaviour or change host emission policy. |
| TST-003 | High | Host diagnostic classification treats fields such as `mode` and `brake_engaged` as universally erroneous and applies inverted/unsuitable thresholds to `heartbeat_ok`; it also reads the packed `0x600` byte as one value. | A correct frame can produce a false error, while a real fault may be hidden. Decode typed fields first, then apply field-specific state rules generated from the contract/test policy. |
| TST-004 | High | [`test_dlc_consistency.cpp`](native-test/test/test_dlc_consistency.cpp) contains hand-copied stale DLCs, including `0x012`, `0x210`, `0x302`, and `0x7FC`. `test_gateway_forwarding.cpp` also copies the old high-to-low list without HMI `0x111`/`0x112`. Both describe themselves as consistency tests. | A passing or failing test does not reliably state production compatibility. Include production generated definitions and generate expectations from the canonical model; eliminate copied protocol tables. |
| TST-005 | Medium | [`test_dlc_generator.py`](native-test/test/test_dlc_generator.py) runs generation in write mode and checks files/selected strings. Its named conflict check does not construct a conflict. | The test can modify the tree and gives false confidence about semantic conflict detection. Test in a temporary output directory with deliberately conflicting fixtures and assert a nonzero result. |
| TST-006 | Medium | Debug-tool simulator schedules and selected behaviours are manually modelled and drift from firmware. | It is useful for existing-tool workflow tests but not firmware equivalence. Separate `synthetic`, `replay`, `virtual CAN`, and `hardware` evidence levels in reports. Only the latter two can support protocol/timing conformance, and only hardware can support physical I/O conformance. |
| TST-007 | High | RT `test_signals.cpp` still tests an older packed-byte interpretation of `0x011`; generated test signal tables collapse YAML's four named light bits into `SYS_LightState`; a debug-tool codec test comment still calls the frame DLC 2. Current YAML and production codec use DLC 3 with ESTOP byte 0, heartbeat byte 1, and four light bits in byte 2. | Stale tests can pass without validating the production layout and make a correct contract change look like a regression. Generate signal fixtures from the canonical model and replace the old `0x011` vector with production-codec golden vectors. |

## 11. Confirmed alignments

The audit is not a statement that everything disagrees. These points currently align and should become regression fixtures:

- Generated artifacts match the current YAML according to `generate_code.py --verify`.
- RT drive command `0x204` is DLC 5 and 100 Hz in YAML and RT scheduling.
- RT brake command `0x205` is DLC 4 and 50 Hz in YAML and RT scheduling.
- Steering request `0x169` is 50 Hz in YAML and implementation; the conflicting architecture/config entries are the stale artifacts.
- RT high- and low-bus `0x7FD` heartbeats use independent counters in implementation and should remain independent in downstream state models.
- SES/SEB receive paths generally perform checksum validation before accepting fault/status content.
- SYS `0x731` L3 fault-bit meanings align with the YAML list.
- MTR target rates for `0x120` (100 Hz) and `0x206` (50 Hz) align with YAML, although hardware initialization is incomplete.
- Root architecture labels MTR as planned/not vehicle-ready, and PWT source labels itself a stub.

## 12. Downstream handoff boundary

The codebase must publish a trustworthy handoff for any future UI, script, analyzer, or automated client:

1. Generated codecs and metadata must represent the bytes actually produced and consumed by firmware.
2. Firmware/build manifests must identify protocol hash, profile, version, and supported capabilities.
3. Raw golden vectors and replay captures must let consumers verify their implementation independently.
4. Firmware diagnostics must distinguish controller state, task health, freshness, corruption, queue loss, and recovery where those facts are observable.
5. Missing capabilities or UART-only evidence must be declared unavailable rather than synthesized.

Adapter management, application APIs, visualization, recording storage, user/LLM access, and UI verdict presentation belong to the separate Control UI project. The codebase remediation neither implements those features nor depends on them for acceptance.

## 13. Closure plan in dependency order

### A. Make the contract real

1. Define and validate a YAML schema with canonical messages and separate bus routes.
2. Add origin/forwarding, timing, checksum, counter, enum, and instance semantics.
3. Generate production C++, host/tool codecs, consumer-neutral metadata, documentation, simulator schedules, and golden vectors from one normalized model.
4. Embed and expose a protocol hash/version.
5. Make CI fail on semantic duplicate conflicts, generated drift, or production code bypassing generated definitions.

### B. Correct known wire mismatches

Resolve `0x210` routing, `0x600` packing, `0x7FE` health bits, `0x7FC` DLC, HMI mode value 2, `0x206` readiness/fault meaning, and SES version layout. Each correction needs a raw-payload golden vector executed by the firmware codec, generated tool decoder, and host codec.

### C. Correct runtime/test-profile claims

Move `SYSTEM_RUN_MODE` into explicit build profiles, implement or remove the SYS watchdog claim, define brake-fault recovery, correct CAN-state reporting, and document exactly what each bench profile bypasses. A hardware-bench profile must retain physical I/O if the goal is to test that I/O.

### D. Complete component capabilities

Finish and validate MTR HAL/CAN/ESTOP initialization. Resolve PWT topology, then implement and validate only the approved standalone or gateway capability set. Until then, capability manifests must report the unresolved functions unavailable and related end-to-end tests remain inconclusive.

### E. Make diagnostics testable

Expose controller state, bus errors, RX/TX/drop counters, task health, command freshness, feedback freshness, and protocol version through supported telemetry/log sources. Add structured, aggregated firmware logs through a documented diagnostic output.

## 14. Acceptance gates

The architecture/YAML/code gap can be considered closed only when all applicable gates pass:

- All production firmware and host targets compile against generated protocol definitions/codecs.
- Generation rejects any semantic mismatch for a message present on multiple routes.
- YAML verification runs read-only in CI and leaves the working tree clean.
- Every frame has golden raw vectors tested by production encoders/decoders and generated tool decoding.
- Vehicle, simulation, CAN-bench, and hardware-bench profiles are explicit, reproducible, and publish a capability manifest.
- A captured protocol hash matches the catalog used by the session.
- Virtual-CAN/replay tests verify loss, corruption, wrong DLC, stale frames, counter discontinuity, bus-off/recovery, backpressure, and log suppression.
- Hardware tests verify actual bus rates, required IDs/DLCs, physical I/O, MTR output/feedback, approved routing/translation behaviour, and reconnect behaviour without unexplained capture drops.
- Test reports contain raw capture references and use `INCONCLUSIVE` when evidence or capability is missing.
- [`architecture.md`](architecture.md), component architecture files, YAML, generated documentation, and executable code contain no unresolved contradictory timing, routing, build-profile, or readiness claims.

## 15. Recommended ownership rule

After migration, authority should be unambiguous:

| Information | Authority |
|---|---|
| Wire IDs, DLC, fields, units, enum values, checksums, counters, timing, origin, routes | Canonical YAML schema/model |
| Encoding/decoding and forwarding tables | Generated code from YAML |
| ECU control behaviour, deadlines, state machines, recovery | Firmware code, traced to architecture requirements |
| Test assertions and golden payloads | Generated contract plus explicit behavioural test policy |
| Observed values, rates, and errors | Versioned raw captures plus test/diagnostic derivation |
| Current/target component readiness | Versioned capability manifest and architecture status |

Architecture explains why and how the system behaves; YAML defines the wire contract; generated code implements that contract; captures prove what ran. None of these layers should silently override another.
