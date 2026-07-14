# Protocol Architecture Migration Plan

> **Authoritative live checklist.** This is the only repository-wide protocol
> remediation task list. [`protocol-testing-plan.md`](protocol-testing-plan.md)
> defines verification and evidence. `codebase-remediation-plan.md` and
> `can-maintainability-work-plan.md` are retained as historical planning records.

## Checklist rules

- A box is checked only when implementation, automated verification, applicable
  builds, documentation, and evidence references are committed.
- Findings use `Open`, `In progress`, `Software closed`, `Blocked on vendor
  evidence`, `Blocked on hardware evidence`, or `Closed`.
- `Software closed` never substitutes for a required capture or physical-I/O
  result. Unsupported capability is explicit, not treated as passing.
- Each implementation commit names the completed outcome rather than a phase.
- Wire changes update contract, generated outputs, producers, consumers,
  vectors, hashes, and documentation atomically.

## Current baseline (2026-07-14)

- [x] `BASE-001` Generated artifacts match the current YAML.
- [x] `BASE-002` Contract/change-discovery validation passes.
- [x] `BASE-003` Current schema and change-tool unit suites pass.
- [x] `BASE-004` The active `0x169` schedule and its configuration constant are
  aligned at 50 Hz; changing this rate later is a reviewed timing change.
- [ ] `BASE-005` Export and commit the normalized `bus + ID` baseline manifest
  and complete language-neutral vectors before contract files move.
- [x] `BASE-006` Run the repository-local native, simulation, RT/SYS/MTR/PWT,
  and debug-tool compatibility baseline defined by the testing plan.
- [ ] `BASE-007` Run the Jetson build in Linux/ROS CI and attach its result; it
  is not representatively buildable in the current Windows environment.

## Objective

Move from the transitional `shared/can` plus `manual-mappings.yaml` design to one static message definition, one explicit codec strategy and shared conformance vectors—without changing wire bytes or deleting useful target behavior prematurely.

This plan changes repository architecture first. Wire-format changes require a separate reviewed change.

## Target invariants

- [ ] Every message layout is defined exactly once.
- [ ] Project-owned messages are grouped by originating ECU; external and multi-sender messages are grouped by protocol family.
- [ ] Every physical occurrence has an explicit bus instance.
- [ ] Runtime identity is `bus + CAN ID`; contract identity is `owner/protocol + message + bus`.
- [ ] Receivers never maintain a second YAML definition.
- [ ] Every message selects exactly one strategy: `generated`, `profile`, or `custom`.
- [ ] Custom/profile messages do not expose a competing ordinary generated codec.
- [ ] Payload decoding, stateful supervision and component fault policy are separate layers.
- [ ] Complex codecs are proven with the same language-neutral vectors in every supported language.
- [ ] PWT participates in the same discovery index even if its manufacturer contract stays separate.

## Stage 1 — Freeze and characterize the current wire contract

Do not reorganize definitions until current behavior is pinned.

- [ ] Record current wire and network hashes.
- [ ] Export a manifest of every `bus + ID`, sender, receivers, DLC and cycle.
- [ ] Add missing independent vectors for all generated E-Trike messages.
- [ ] Add dedicated vectors for SES command/status/error/version/test.
- [ ] Add dedicated vectors for SEB command/status/error/version/test.
- [ ] Add PWT extended-frame vectors.
- [ ] Add counter sequence vectors independently from payload vectors.
- [x] Use the current implemented/YAML `0x169` rate of 50 Hz and remove the stale
  100 Hz configuration claim. Any later change must update timing metadata,
  scheduler, vectors, and measured acceptance limits together.
- [ ] Require all current native and firmware builds to pass before structural migration begins.

Exit gate: current bytes and known algorithms can be reproduced without relying on the implementation being migrated.

## Stage 2 — Introduce the portable protocol core

Create the new structure without moving contracts yet:

```text
protocol/
├── core/
├── contracts/
├── generated/
├── profiles/
├── codecs/
├── vectors/
├── tools/
└── tests/
```

- [ ] Move or recreate `Frame`, immutable `FrameView`, `CodecStatus` and bit helpers under `protocol/core`.
- [ ] Keep core free of ESP-IDF, FreeRTOS, ROS, logging and component global state.
- [ ] Split shared enums from legacy DTOs.
- [ ] Make generated codecs depend only on protocol core.
- [ ] Add include-boundary tests that reject platform dependencies from protocol core.
- [ ] Add temporary compatibility includes so RT/SYS/MTR continue building during migration.

Exit gate: generated ordinary codecs compile without including legacy `can_protocol.h`.

## Stage 3 — Extend the schema with ownership, instances and strategy

Add normalized fields before physically dividing files:

```yaml
owner: RT
name: RT_HEARTBEAT
codec: {strategy: generated}
instances:
  - {bus: high, id: 0x7FD, receivers: [Host], state_scope: independent}
  - {bus: low,  id: 0x7FD, receivers: [SYS],  state_scope: independent}
```

- [ ] Add stable owner/protocol keys.
- [ ] Add explicit bus instances.
- [ ] Add `same_frame`, `regenerated`, and `independent` instance semantics.
- [ ] Add required codec strategy.
- [ ] Add versioned implementation/profile ID for non-generated strategies.
- [ ] Validate uniqueness by `bus + ID`.
- [ ] Validate one layout definition per canonical message key.
- [ ] Reject a custom/profile message without an implementation and vector set.
- [ ] Reject more than one payload strategy.

Exit gate: the existing YAML can be normalized into the new model without changing generated bytes.

## Stage 4 — Divide contracts without duplicating messages

Create:

```text
protocol/contracts/network.yaml
protocol/contracts/host.yaml
protocol/contracts/rt.yaml
protocol/contracts/sys.yaml
protocol/contracts/mtr.yaml
protocol/contracts/ses.yaml
protocol/contracts/seb.yaml
protocol/contracts/pwt.yaml
```

- [ ] Put buses, nodes, bitrate and forwarding references in `network.yaml`.
- [ ] Move each project message to its originating sender file.
- [ ] Move multi-sender/factory messages to SES/SEB protocol files.
- [ ] Move the PWT/DC-DC manufacturer definition without converting its extended frame to an application message.
- [ ] Represent forwarded routes by reference; do not copy payload layouts.
- [ ] Compare pre/post per-instance hashes and generated bytes.
- [ ] Make `inspect 0xID` require a bus when the ID is ambiguous.

Exit gate: no canonical message key has more than one layout definition, and all pre-migration vectors still pass.

## Stage 5 — Implement codec strategies

### Generated messages

- [ ] Generate metadata and complete codec.
- [ ] Cover exact ID, frame type, DLC, endian, range, enum and constants.

### Named profiles

- [ ] Define a small registry of versioned code implementations such as `vendor_xor8_v1`.
- [ ] Prohibit arbitrary YAML expressions or scripts.
- [ ] Require profile-level C++ and Python vectors.

### Custom codecs

- [ ] Implement complete SES codecs without calling legacy `to_frame/from_frame`.
- [ ] Implement complete SEB codecs without calling legacy `to_frame/from_frame`.
- [ ] Implement/retain the complete PWT manufacturer codec.
- [ ] Use generated metadata for offsets, widths and constants where practical.
- [ ] Leave output unchanged on every decode failure.
- [ ] Make ordinary generated payload codecs unavailable for custom messages.

Exit gate: every message has one callable payload path and no competing legacy/generated alternative.

## Stage 6 — Separate supervision and component policy

- [ ] Add protocol-neutral `CounterTracker` and `FreshnessTracker` interfaces.
- [ ] Key state by bus, CAN ID, expected producer and session.
- [ ] Test wrap, duplicate, gap, reorder, reset and recovery using sequence vectors.
- [ ] Keep RT takeover/ESTOP decisions in RT policy.
- [ ] Keep SYS brake/fault decisions in SYS policy.
- [ ] Keep backend presentation and test verdicts outside codecs.
- [ ] Derive nominal timing from message instances while retaining allowed misses as component policy.

Exit gate: payload codecs are stateless and contain no vehicle response policy.

## Stage 7 — Add Control UI language support

- [ ] Generate Python codecs for generated messages.
- [ ] Implement Python versions of required profiles/custom codecs.
- [ ] Run the same vector documents against C++ and Python.
- [ ] Expose wire hash, strategy, implementation ID and vector version through backend capabilities.
- [ ] Mark a message unsupported if its selected Python codec is unavailable.
- [ ] Allow raw monitoring but block decoded injection for unsupported or mismatched codecs.

Exit gate: backend and firmware agree on every vector for messages the UI can decode or transmit.

## Stage 8 — Replace discovery and CI enforcement

- [ ] Generate the discovery index directly from contract definitions.
- [ ] Remove hand-maintained consumer lists.
- [ ] Discover code impact using codec/type symbols and build dependencies.
- [ ] Report ambiguous IDs by bus instance.
- [ ] Validate implementation and vector targets, not merely file existence.
- [ ] Reject direct payload access outside generated/profile/custom codec locations.
- [ ] Reject production includes of legacy DTOs.
- [ ] Build RT, SYS, MTR, PWT and Jetson in their supported environments.
- [ ] Keep HIL evidence separate from software conformance.

Exit gate: CI proves strategy uniqueness, implementation availability, vector coverage and dependency boundaries.

## Stage 9 — Retire transitional infrastructure

- [ ] Remove whole-file scanner exemptions.
- [ ] Remove `manual-mappings.yaml` after every entry is represented by message strategy and vectors.
- [ ] Remove command adapters that wrap legacy codecs.
- [ ] Remove handwritten application DTOs from `can_protocol.h`.
- [ ] Replace `can_protocol.h` with temporary compatibility forwarding headers, then remove it.
- [ ] Move generated artifacts and tools from `shared/can` into `protocol`.
- [ ] Update all architecture, test and developer documentation.

Exit gate: `shared/can` and the manual registry are no longer production authorities or required compatibility paths.

## Change sequence per message

Migrate one message family at a time:

1. Heartbeats and ordinary application reports.
2. Host commands.
3. RT/SYS/MTR commands and feedback.
4. SES status/test/version/error, then SES command.
5. SEB status/test/version/error, then SEB command.
6. PWT/DC-DC extended command.

For each family:

- [ ] Pin vectors before implementation changes.
- [ ] Add strategy and implementation ID.
- [ ] Migrate every language implementation.
- [ ] Build all senders and receivers.
- [ ] Remove the old payload path.
- [ ] Prove no raw-byte consumer bypass remains.

## Completion criteria

- [ ] One layout definition exists for every canonical message.
- [ ] Host/RT, RT/MTR/SYS and other sender/receiver pairs consume the same definition.
- [ ] Bus-specific identity and independent state are explicit.
- [ ] Every message exposes exactly one payload strategy.
- [ ] Complex algorithms pass common C++ and Python vectors.
- [ ] Stateful supervision and component policy remain separate.
- [ ] PWT appears in repository-wide inspection and verification.
- [ ] No production code depends on transitional manual mappings or legacy DTOs.

## Evidence index

When work is completed, append a row rather than placing evidence only in a
commit message.

| Task or gap | Status | Implementation | Automated evidence | External evidence |
|---|---|---|---|---|
| `BASE-001`–`BASE-003` | Closed | Current generator and discovery tools | `generate_code.py --verify`; `can_change.py verify`; 7 Python tests | Not applicable |
| `BASE-006` | Closed | Current repository consumers | 18/18 native; 435/435 simulation; RT/SYS/MTR/PWT vehicle builds; debug shared 102, backend 175, UI 78 | Jetson is tracked separately by `BASE-007` |
| `TIM-001` / `BASE-004` | Software closed | YAML, RT 50 Hz TX block, `config.h` | Contract generation and RT/native build gates | Physical period capture remains part of bench acceptance |
| `FRM-007` | Blocked on vendor evidence | Raw frame support only is trustworthy | Raw identity/DLC tests | Vendor definition or known-response vector required |

The detailed case matrix, CI tiers, evidence schema, verdicts, and hardware
procedures are maintained in [`protocol-testing-plan.md`](protocol-testing-plan.md).
