# rt-esp32-rs

Rust firmware for the E-Trike RT node (ESP32‑S3): dual-bus gateway, tricycle
kinematics, steering, and brake intent. RT is the fleet's only dual-bus node.

See `architecture.md` for the full design.

## Status

Phase P4 scaffold. The shell honours SYS authority (asymmetric ESTOP clear via
`etrike_core::rt::AuthorityGate`) and withholds motion otherwise. Steering
(`0x169`), the MCP2515 High bus, and the gateway are the next work items; their
decision logic will live in `etrike_core::rt`.

## Layout

- `src/lib.rs` — platform-independent shell and authority gating (host testable).
- `src/hal.rs` — ESP‑S3 hardware binding behind the `hal` feature (**to do**).

## Host tests (no hardware)

```
cargo test
```

## Building for the ESP32‑S3

Requires `espup` and the `xtensa-esp32s3-none-elf` target (see
`../sys-esp32-rs/README.md`). The High bus uses an external MCP2515 over SPI
(driver to be written over `embedded-hal-async`); the Low bus uses the on-chip
TWAI peripheral.

## `0x7B9` policy

RT never transmits `0x7B9`. It emits brake intent only (`0x205`); SYS is the sole
producer of `0x7B9`. The C++ emergency-takeover fallback was dead code and is
intentionally not ported.
