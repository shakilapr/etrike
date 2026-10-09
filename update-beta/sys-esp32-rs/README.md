# sys-esp32-rs

Rust firmware for the E-Trike SYS node (ESP32‑S3): safety, mode, power, and
body control. SYS is the sole mode/ESTOP authority and the sole normal producer
of `0x7B9`.

See `architecture.md` for the full design.

## Layout

- `src/lib.rs` — platform-independent shell: assembles `SysInputs` from verified
  frames + debounced GPIO, owns the `SysCore`, and emits `SysOutputs`. Host
  testable with a mock HAL.
- `src/hal.rs` — the ESP‑S3 hardware binding (TWAI, GPIO, WS2812) behind the
  `hal` feature. **Not yet implemented** (this is on-target work; see below).
- Decision logic lives in the `etrike-core` crate, not here.

## Host tests (no hardware)

```
cargo test          # runs the shell logic + core invariants with a mock HAL
```

## Building for the ESP32‑S3

Requires the Xtensa Rust toolchain and an ESP target:

```
cargo install espup
espup install
. ~/export-esp.sh          # (Windows: see esp-rs book)
cargo build --target xtensa-esp32s3-none-elf
```

Then flash with `espflash`.

### Wiring the HAL (`src/hal.rs`, feature `hal`)

1. Add the ESP dependencies to `Cargo.toml` (enable the `hal` feature):
   - `esp-hal = { version = "1", features = ["esp32s3", "unstable"] }`
   - the embassy executor/runtime crate (`esp-rtos`, or `esp-hal-embassy` for
     older esp-hal releases — note `esp-hal-embassy` is deprecated in favour of
     `esp-rtos`)
   - `embassy-sync`, `embassy-time`
2. Implement `SysHal` for a struct that owns the TWAI driver, the GPIO inputs,
   the relay/lamp outputs, and the WS2812 strip.
   - `poll_frame` must `verify()` each frame (DLC/CRC/route/sender) and gate
     request streams with `StreamValidity` before returning them.
   - `send_frame` only emits IDs owned by SYS (see `TX_OWNERS`).
3. `#[esp_hal::main] fn main()` initialises the HAL, constructs `SysCore`, and
   calls `run(&mut hal, &mut core)`.

The C++ reference (disposition of every task and constant) is in
`architecture.md` §10 and `../sys-esp32/src/config.h`.
