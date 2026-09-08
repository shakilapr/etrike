# One-Week ESTOP Compatibility Audit (2026-09-08)

Scope: review the last ~7 days of ESTOP-architecture work across firmware,
protocol and ControlToolbox, and confirm everything is aligned with the
implemented "new reality".

## What shipped this week (reference)
- `0x001` SAFETY_ESTOP stays DLC-0 universal STOP NOW (rate-limited, loopback-ignored).
- `0x011` SYS_SAFETY_STS = persistent ESTOP authority: DLC 5, `estop_active`,
  `rolling_counter`, AUTOSAR E2E CRC-8 (poly 0x2F, Data-ID 0x3C11). RT/MTR clear only
  on two *advancing* `estop_active=0` frames; MTR additionally requires the `0x113`
  OFF→ON REARM edge.
- Three-name speed scheme: `0x204.motor_speed_mmps` (commanded) /
  `0x206.motor_command_speed_mmps` (MTR echoed command — renamed) /
  `0x122.measured_speed_mmps` (physical wheel, opt-in).
- New observational NODE_STATUS frames `0x500` SYS / `0x501` RT / `0x502` MTR.
- `sys-esp32` inhibit_state atomics + validated atomic ESTOP exit (#7); MTR IWDG (#4);
  RT explicit SYS `0x011` authority acquisition (#10) + independent brake-stream watch (#5).

## Compatibility assessment
- Firmware: sys/rt/mtr already current; this week added NODE_STATUS emission on all
  three, gated rm-esp32's `0x011` authorship to the bench build (consumes real SYS
  `0x011` in the vehicle build), and verified the Jetson `messages::` binding maps to the
  same generated protocol (estop DLC-0 + 500 ms rate limit).
- Protocol codec (python): could not *author* `0x011`/NODE_STATUS because
  `rolling_counter`/`e2e_crc` are required fields and no CRC/counter was generated.
  Fixed in ControlToolbox `encoder.encode_message` (`auto_counter`/`auto_e2e`, two-pass
  AUTOSAR CRC), which restored the SYS SIL peer.
- ControlToolbox: now observes NODE_STATUS latch (report + monitor + UI), probes MTR via
  `0x502`, consults `node_state` in the motion gate, exposes a bench REARM endpoint, and
  surfaces the `0x122` measured-speed source. Remaining intentionally-fixed literals
  (topology/QA scripts) are protocol-consistency probes that *should* fail on drift.

## Still open (by design / hardware / architecture)
- `0x001` sender/reason/epoch payload (issue #11), RT positive `0x011` acquisition gate
  (#14), distinct brake source IDs (#3 option-3), wheel-encoder physical EGAS (#3/#9 —
  no encoder fitted), independent HW watchdog on non-MTR nodes (#6), NODE_STATUS CRC
  Data-ID convention (frozen vectors carry an opaque byte), pwt-esp32 ESTOP handling
  (deferred by decision).
