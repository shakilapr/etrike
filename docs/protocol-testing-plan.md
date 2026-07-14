# Protocol Verification and Evidence Plan

## Purpose

This document defines how the CAN contract and its consumers are proven. The
live implementation checklist is
[`protocol-architecture-migration-plan.md`](protocol-architecture-migration-plan.md).
This document describes tests and evidence; it is not a second task list.

No test level is promoted beyond what it observes. Schema and codec tests prove
bytes, virtual buses prove software interaction, and hardware capture proves
physical timing and I/O. A simulator result is never hardware evidence.

## Evidence levels

| Level | Proves | Does not prove |
|---|---|---|
| Schema/compiler | Valid normalized contract and deterministic generation | Runtime scheduling or hardware |
| Codec vectors | Exact cross-language payload behavior | Transport, deadlines, or ECU policy |
| Native component | State machines, counters, deadlines, and aggregation | Target peripherals and real-time timing |
| Replay | Deterministic decoding and historical compatibility | Live transmission or connectivity |
| Virtual dual CAN | Routing, loss/corruption handling, and ECU interaction | USB adapter or physical bus behavior |
| Adapter characterization | Channels, timestamps, queues, unplug/replug, and loss visibility | ECU hardware behavior |
| HIL/bench | Flashed firmware, measured bus timing, and exercised physical I/O | Paths not exercised by that procedure |
| Soak/fault injection | Bounded resources, recovery, and log suppression | Untested electrical extremes |

Every result records its evidence level. `Pass` means only that the assertions
at that level passed. Missing required evidence produces `Inconclusive` or
`Blocked`, never `Pass`.

## Contract and generator suite

The suite must cover:

- valid definitions and deliberate failures for duplicate canonical keys,
  duplicate `bus + ID`, overlapping fields, fields outside DLC, invalid enum or
  range combinations, ambiguous routes, and cross-bus semantic conflicts;
- exactly one `generated`, `profile`, or `custom` codec strategy per message;
- implementation ID and vector-set presence for every profile/custom strategy;
- deterministic byte-for-byte output with no wall-clock content;
- read-only verification and a clean tracked worktree after generation/build;
- a semantic wire hash that ignores formatting/comments and changes when wire
  meaning changes;
- routes and discovery generated from definitions rather than copied tables;
- ambiguous ID inspection requiring an explicit bus.

Required commands:

```text
python shared/can/generate_code.py --verify
python tools/can_change.py verify
python -m unittest shared/can/test_codegen_contract.py
python -m unittest shared/can/test_change_tool.py
```

During the directory migration these commands remain compatibility entrypoints.
They must ultimately call the protocol package tools rather than retain a second
implementation.

## Codec conformance suite

Language-neutral vector documents are the oracle. Each vector identifies the
canonical message, bus instance where relevant, strategy, implementation
version, DLC, frame format, raw bytes, typed values, and expected status.

Every message receives zero/default, representative, minimum, maximum,
signed-negative, endian-sensitive, enum, reserved-bit, and roundtrip cases where
those concepts apply. Negative cases cover short/long DLC, wrong standard or
extended format, invalid enum, invalid constant, invalid checksum, invalid
range, and truncated buffers. A failed decode must leave the caller's output
unchanged.

SES, SEB, and PWT require independent vectors for every command, status, error,
version, and test frame that is claimed as supported. Until a trusted SES
version vector exists, version bytes are raw-only and semantic decoding is
reported as unsupported.

The same vector documents run against C++, Python, and TypeScript. A language
may omit an unsupported custom codec, but capabilities must then mark decoded
transmission and formal semantic verdicts unavailable.

## Stateful supervision suite

Counter and freshness tests are separate from payload vectors. They cover:

- first sample, normal increment, wrap, allowed duplicate, frozen value, gap,
  reorder, saturation, ECU restart, adapter/session epoch change, and recovery;
- independent state for identical IDs on different buses;
- startup grace, allowed misses, monotonic deadline wrap, and recovery
  hysteresis;
- traffic present while producer counter or ECU task is frozen;
- adapter open but silent, unplugged, reopened, and receiving after a new epoch.

Trackers are keyed by bus, ID, expected producer, and session/adapter epoch.
Codec validation remains stateless; RT/SYS/MTR response policy is tested in its
own component suite.

## Component and integration suite

Native and target tests cover:

- RT event-queue saturation, coalescing/drop accounting, gateway queue drops,
  both heartbeat instances, command staleness, and recovery;
- SYS task stalls, SEB counter windows, brake-fault latch/reset rules, controller
  `error_active`, `error_passive`, `bus_off`, `recovering`, and recovery;
- MTR checked TX failures, bounded ADC/I2C timeouts, current failure streak,
  cumulative failures, direct ESTOP behavior, and stale command handling;
- PWT standard/extended identity, DLC, generated metadata, TX failure, and its
  approved standalone topology;
- replay and virtual dual-CAN routing, corruption, loss, reconnect,
  backpressure, and protocol-hash mismatch;
- compatibility of the existing debug tool until Control UI replaces it.

A persistent high-rate fault must emit the first event immediately, bounded
updates/summaries with exact suppressed counts, and one recovery event. Raw CAN
recording is separate and is not duplicated into operational logs.

## CI gates

### Pull requests

- contract/schema validation and deterministic read-only generation;
- C++, Python, and TypeScript vectors available for the changed strategy;
- native CTest, simulation, and existing debug-tool compatibility suites;
- RT, SYS, MTR, and PWT supported profile builds;
- Jetson build in Linux/ROS CI;
- scans rejecting new legacy DTO use, raw payload access outside codec
  boundaries, unexplained CAN literals, and platform dependencies in core;
- clean tracked worktree after checks and builds.

### Nightly

- replay and virtual dual-CAN fault matrices;
- property/fuzz tests for codecs and counter sequences;
- queue, scheduler, recorder, and bounded-log soak tests;
- all supported build profiles and compatibility clients.

### Labelled bench workflow

- adapter fingerprint, channel mapping, bitrate, standard/extended frames, DLC
  0-8, timestamp behavior, ordering, queue overflow, unplug/replug, and reopen;
- measured ECU periods/jitter, bus-off and recovery, command/feedback latency,
  and the physical inputs/outputs explicitly named by the procedure;
- raw capture plus manifest attached to the tested commit.

## Evidence record and verdict

Each formal run records commit, wire/network hash, vector version, firmware
version, build profile, capability manifest, hardware revision, adapter and
driver identity, channel mapping, bitrate, wall and monotonic time bases,
capture/queue drops, test inputs, assertions, and artifact locations.

Verdicts are `Pass`, `Fail`, `Inconclusive`, `Blocked`, or `Not applicable`.
Evidence quality is independently `Complete`, `Incomplete`, `Degraded`, or
`Incompatible`. A pass with incomplete or incompatible evidence cannot close a
gap.

## Hardware and vendor gates

Repository tests may mark a finding `Software closed`; the following still need
external evidence:

- SES version byte interpretation: trusted vendor definition or known hardware
  response and a definitive raw vector;
- MTR clock/GPIO/CAN/ADC/I2C initialization, direct ESTOP electrical level, and
  actuator/peripheral behavior: board and harness bench evidence;
- RT/SYS/MTR/PWT physical timing, controller error states, and recovery: labelled
  captures from flashed artifacts;
- protocol identity over CAN: a separately reviewed wire-contract change unless
  an existing diagnostic/version frame is approved for it.

The gap register must name the owner, blocking evidence, and exact procedure for
each such item instead of leaving an unchecked generic task.
