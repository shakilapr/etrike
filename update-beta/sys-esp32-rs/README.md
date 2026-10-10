# sys-esp32-rs

Rust firmware for the E-Trike SYS node (ESP32‑S3): safety, mode, power, and
body control. SYS is the sole mode/ESTOP authority and the sole normal producer
of `0x7B9`.

See `architecture.md` for the full design.

## Layout

- `src/lib.rs` — platform-independent shell: assembles `SysInputs` from verified
  frames + debounced GPIO, owns the `SysCore`, and emits `SysOutputs`. Host
  testable with a mock HAL.
- `src/hal.rs` — the ESP‑S3 hardware binding (`SysHalImpl`): on-chip TWAI
  (Low bus), debounced safety/body inputs, and the 10 ms control tick.
  Compiled only for `target_arch = "xtensa"`.
- `src/main.rs` — `#[esp_rtos::main]` entry point. Built only with
  `--features hal`.
- Decision logic lives in the `etrike-core` crate, not here.

## Host tests (no hardware, no ESP toolchain)

```
cargo test          # shell logic + core invariants with a mock HAL
```

## Bench bypass

`main.rs` sets `const SAFETY_BYPASS: bool = true;` — every command passes
straight through (no ESTOP, no clamps, no reset blockers, and inbound frames are
not verified). Power defaults **ON** without the physical run latch; the host can
switch it off via `0x112`, and mode follows the host `0x111` request. Set it to
`false` for a safety-enforcing build.

## Building for the ESP32‑S3

Requires the Xtensa Rust toolchain (`espup`):

```
cargo install espup espflash --locked
espup install --targets esp32s3
. ~/export-esp.sh            # Windows: . $HOME/export-esp.ps1
cargo +esp build --release --target xtensa-esp32s3-none-elf -Zbuild-std=core --features hal
espflash flash --monitor --chip esp32s3 target/xtensa-esp32s3-none-elf/release/sys-esp32-rs
```

### Toolchain note (link step)

The crate **compiles** cleanly against `esp-hal ~1.2.2` / `esp-rtos 0.4`.
The final **link** currently fails with `linker script file 'memory.x'
appears multiple times` on the `xtensa-esp-elf-gcc` **16.2.0** that the current
`espup` installs — the same failure occurs with the upstream `esp-generate`
template (verified), so it is a toolchain/binutils mismatch, not a code issue.
Use the GCC version that shipped with the `esp-hal 1.2.2` release (or the
`espflasher`/`esp-rs` toolchain from that era) to link and flash.

### Hardware bring-up checklist

- TWAI: RX = GPIO4, TX = GPIO5, 500 kbit/s.
- Inputs (fail-safe): ESTOP GPIO1 (NC, active-high-on-open), START GPIO41
  (NC latching), lever GPIO2 (active-low), MODE GPIO11, switches GPIO9/6/7.
- `poll_frame` must `verify()` each frame (DLC/CRC/route/sender) and gate
  request streams with `StreamValidity` before returning them.
- `send_frame` only emits IDs owned by SYS (see `TX_OWNERS`).

The C++ reference (disposition of every task and constant) is in
`architecture.md` §10 and `../sys-esp32/src/config.h`.
