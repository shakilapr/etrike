# `etrike-protocol` — Wire Contract Crate

**Layer:** Wire (bottom).
**Status:** Phase −1 design.
**Responsibility:** Own the canonical CAN wire contract and validate frames.
Nothing else. No policy, no hardware, no state machines.

---

## 1. Purpose

`etrike-protocol` is the Rust face of the fleet's wire contract. It is generated
from `protocol/contracts/*.yaml` and wrapped with the validation primitives that
every pipeline must pass through. It answers exactly three questions:

1. How do I turn a typed message into CAN payload bytes?
2. How do I turn CAN payload bytes into a typed message, or reject them?
3. Given a `(bus, id)`, which node is allowed to send it, and where does it go?

Everything the core and shell do with CAN flows through this crate. Nothing in
it knows about modes, ESTOP policy, steering, or brakes.

---

## 2. Position in the workspace

```
shell (sys-esp32-rs / rt-esp32-rs)
   │  uses RawFrame, verify(), encode/decode, StreamValidity
   ▼
etrike-core        (depends on etrike-protocol types only)
   ▲
   │
etrike-protocol    (this crate; no dependencies on core or shell)
```

The crate has **no upward dependencies**. It does not import `etrike-core` or
any HAL.

---

## 3. Source of truth and generation

The crate's payload codecs are **generated**, never hand-written.

```
protocol/contracts/*.yaml
        │  (authoritative wire facts: id, dlc, byte order, signals, enums, counters)
        ▼
protocol/tools/protocol.py::_render_rust
        ▼
protocol/generated/rust/etrike_protocol.rs   (checked into the repo)
        ▼
etrike-protocol/src/lib.rs   (includes + wraps the generated module)
```

- The emitter is added alongside the existing `_render_cpp` in
  `protocol/tools/protocol.py` and registered in `render_outputs`, so the
  existing `generate --check` drift gate covers Rust automatically.
- Generated output is never edited by hand. If a field is wrong, the contract
  YAML is wrong, and regeneration fixes all languages at once.
- SES/SEB vendor frames use the `custom` codec strategy (opaque layouts); the
  Rust emitter mirrors the C++ `codecs/{ses,seb}` behaviour for those keys.

---

## 4. Public API

### 4.1 Frame representation

```rust
pub struct RawFrame {
    pub bus: Bus,        // High | Low | Powertrain
    pub id: u32,         // 11-bit standard
    pub dlc: u8,         // 0..=8
    pub data: [u8; 8],   // significant bytes = dlc
}
```

`RawFrame` is a passively-copied value. It has no methods that act on the
vehicle; it exists only to be verified.

### 4.2 Verification

```rust
pub enum CodecStatus {
    Ok,
    WrongMessageId,
    WrongFrameFormat,
    UnexpectedLength,
    ValueOutOfRange,
    InvalidEnum,
    ConstantMismatch,
    ChecksumMismatch,
    UnsupportedSemantics,
}

pub enum Verified<T> {
    Ok(T),
    Rejected(CodecStatus),
}

/// Reject bad DLC, bad E2E CRC, unknown route, or wrong sender.
pub fn verify(frame: &RawFrame) -> CodecStatus;

/// Decode into a typed message after DLC/CRC checks.
pub fn decode<T: Message>(frame: &RawFrame) -> Result<T, CodecStatus>;

/// Encode a typed message. Fails if the frame format is wrong.
pub fn encode<T: Message>(msg: &T, frame: &mut RawFrame) -> Result<(), CodecStatus>;
```

`verify` is intentionally separate from `decode` so the shell can cheaply
reject malformed traffic (and count it as an RX-overflow diagnostic) before
doing any work.

### 4.3 Typed messages

For every `generated` message in the contracts, the emitter produces a struct
whose fields carry enums for bounded signals:

```rust
pub struct SysSafetySts {
    pub estop_source: EstopSource,   // enum, not u8
    pub node_presence: u8,
    pub estop_reason: EstopReason,   // enum, not u8
    pub rolling_counter: u8,
}
```

- Multi-byte fields honour the contract byte order (all E-Trike messages are
  big-endian; SES/SEB vendor frames carry their own mixed order).
- Signed signals decode through sign-extension, not truncation.
- Enumerated signals decode into Rust enums; an out-of-range value yields
  `InvalidEnum` rather than silently passing a bad number to the core.

### 4.4 Routing and TX ownership

The emitter emits two `const` tables derived from `network.yaml`:

```rust
pub struct MessageMeta { pub key: &'static str, pub bus: Bus, pub id: u32, pub dlc: u8, pub strategy: CodecStrategy }
pub struct RouteMeta { pub key: &'static str, pub message: &'static str, pub from: Bus, pub to: Bus }

pub const MESSAGES: &[MessageMeta];
pub const ROUTES: &[RouteMeta];
pub const TX_OWNERS: &[TxOwner];   // (bus, id) -> owner node
```

- `TX_OWNERS` enforces the "one sender per `(bus, id)`" rule. The shell asserts
  it at startup; a task that tries to send an ID it does not own is a bug caught
  before any frame leaves the wire.
- `ROUTES` drives the RT gateway; the core never decides routing by ID ad hoc.

### 4.5 Stream validity

Port of `shared/stream_validity.h`, used for host/HMI requests and peer status
streams:

```rust
pub struct StreamValidity { /* last counter, baseline flag */ }

impl StreamValidity {
    pub fn observe(&mut self, counter: u8) -> StreamStatus;
}

pub enum StreamStatus {
    Init,       // no baseline yet
    Valid,      // delta == 1
    ValidGap,   // delta == 2 (one frame may be lost)
    Stale,      // delta == 0 (repeat / frozen)
    Fault,      // delta > 2 (sequence break)
}
```

Rule: delta 0 → no refresh, rejected; 1 → accepted; 2 → accepted with one miss;
>2 → sequence fault, requiring reacquisition (baseline then an advancing frame).
This is what prevents a frozen or replayed host frame from being treated as a
live command.

### 4.6 Freshness

```rust
pub struct Timestamped<T> { pub value: T, pub at: Instant }
pub fn fresh(ts: &Timestamped<impl Sized>, now: Instant, window: Duration) -> bool;
```

Used by the core for command staleness (e.g. host drive command, brake intent).

---

## 5. Validation pipeline

```
                          RawFrame
                             │
                 ┌───────────▼───────────┐
                 │ verify()               │  DLC, E2E CRC, route, sender
                 └───────────┬───────────┘
              Rejected ◄─────┤
                             │ Ok
                 ┌───────────▼───────────┐
                 │ decode()  ->  TypedMsg │  byte order, enums, sign-extend
                 └───────────┬───────────┘
              Rejected ◄─────┤
                             │ Ok
                 ┌───────────▼───────────┐
                 │ StreamValidity + fresh │  counter rule + time window
                 └───────────┬───────────┘
              Stale/Fault ◄──┤
                             │ Valid
                             ▼
                       Valid<TypedMsg>  ──►  etrike-core Inputs
```

Only `Valid<TypedMsg>` ever crosses into `etrike-core`. An unverified or stale
frame has no path to influence an output.

---

## 6. Features and configuration

| Feature | Effect |
|---------|--------|
| `defmt` | Derive `defmt::Format` on generated and wrapper types for firmware logging |
| `std`   | Enabled only for host tests; brings `std::time` for test harnesses |

The crate is `#![no_std]` by default. It must build for
`xtensa-esp32s3-none-elf`, `thumbv*` (future STM32), and the host test target.

---

## 7. Testing

**Conformance (host `cargo test`).** The Python test corpus already ships
language-neutral vectors:

- `protocol/vectors/payload-v1.json` — field values and resulting payload bytes.
- `protocol/vectors/custom-codec-values-v1.json` — SES/SEB opaque codecs.
- `protocol/vectors/sequences-v1.json` — counter/validity sequences.

Each vector is compiled into a Rust test that encodes a typed message and
asserts the exact bytes, then decodes and asserts the field values.

**Round-trip properties.** For every generated message: `decode(encode(x)) == x`
for values within the contract's min/max, and `encode(decode(bytes)) == bytes`
for canonical payloads.

**Rejection tests.** Wrong DLC, wrong CRC, unknown `(bus, id)`, wrong sender,
out-of-range enum, and constant-mismatch all return the specific `CodecStatus`.

**Ownership test.** `TX_OWNERS` contains no duplicate `(bus, id)`.

---

## 8. Non-goals

- No interpretation of message meaning (no "this means ESTOP").
- No timing, scheduling, or retransmission logic.
- No hardware access. The crate never touches TWAI, SPI, or GPIO.
- No policy: it does not decide what is safe.

---

## 9. References

- `protocol/README.md` — contract model and codegen tooling.
- `protocol/contracts/*.yaml` — the wire facts this crate is generated from.
- `protocol/vectors/*.json` — conformance corpus.
- `shared/stream_validity.h` — the C++ original this crate ports.
- `can-dictionary.md` — human-readable message catalog.
