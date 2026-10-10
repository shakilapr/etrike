# rt-esp32-rs

Rust firmware for the E-Trike RT node (ESP32‑S3): dual-bus gateway, tricycle
kinematics, steering, and brake intent. RT is the fleet's only dual-bus node.

See `architecture.md` for the full design.

## Status

- Shell logic (authority gating with the asymmetric 2-frame ESTOP clear) is
  host-tested with a mock HAL.
- `src/hal.rs` implements the ESP‑S3 binding: on-chip TWAI (Low bus) and an
  MCP2515 driver over SPI (High bus), plus the 5 ms control tick. Compiled only
  for `target_arch = "xtensa"`.
- Steering (`0x169`), the gateway task split, and kinematics land in
  `etrike_core::rt` next; the frame encode/decode and authority gate are
  already ported and tested.

## Layout

- `src/lib.rs` — platform-independent shell and authority gating (host testable).
- `src/hal.rs` — ESP‑S3 hardware binding (`RtHalImpl`, MCP2515 driver).
- `src/main.rs` — `#[esp_rtos::main]` entry point (built with `--features hal`).

## Host tests (no hardware)

```
cargo test
```

## Bench bypass

`main.rs` sets `const SAFETY_BYPASS: bool = true;` — motion authority is always
granted (host drive passes straight through) and inbound frames are not
verified. A commanded **Neutral gear (0) forces zero speed**, and a stale or
absent host drive command (past `DRIVE_LOSS_MS`) zeroes the output. Set it to
`false` for a safety-enforcing build.

## Building for the ESP32‑S3

Requires `espup` and the `xtensa-esp32s3-none-elf` target:

```
cargo +esp build --release --target xtensa-esp32s3-none-elf -Zbuild-std=core --features hal
```

**Toolchain note:** the crate compiles against `esp-hal ~1.2.2` / `esp-rtos 0.4`,
but the final link currently fails with `memory.x appears multiple times` on the
`xtensa-esp-elf-gcc` 16.2.0 installed by the current `espup` — the same failure
occurs with the upstream `esp-generate` template, so it is a toolchain/binutils
mismatch, not a code issue (see `../sys-esp32-rs/README.md`).

## Buses

- Low bus: on-chip TWAI, RX = GPIO4, TX = GPIO5, 500 kbit/s.
- High bus: external MCP2515 over SPI — SCK 15, MOSI 16, MISO 17, CS 18,
  INT 47, 500 kbit/s @ 8 MHz oscillator.

## `0x7B9` policy

RT never transmits `0x7B9`. It emits brake intent only (`0x205`); SYS is the sole
producer of `0x7B9`. The C++ emergency-takeover fallback was dead code and is
intentionally not ported.
