//! ESP32-S3 hardware binding for SYS (feature `hal`, target-only).
//!
//! Not yet implemented. This module is compiled only when both the `hal`
//! feature is enabled and the target is `xtensa`. See `README.md` for the
//! wiring checklist (TWAI, GPIO inputs, relay/lamp outputs, WS2812).
//!
//! Planned contents:
//! - a `SysHalImpl` owning the TWAI driver, debounced GPIO, and WS2812;
//! - `impl SysHal for SysHalImpl` (poll/verify frames, send owned frames);
//! - `#[esp_hal::main] fn main()` that constructs `SysCore` and calls
//!   `crate::run`.
