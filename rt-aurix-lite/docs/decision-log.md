# RT-AURIX-Lite — Decision Log

Key decisions, corrections, and resolved-open-items in the consolidated
RT-only AURIX design. Each entry records what changed and why.

## 2026 — CAN_HIGH module/node frozen (from iLLD pinmap)

**Decision:** The high-level CAN bus is **CAN0 Node 2** on `P15.0`/`P15.1`.

**Why:** `aurix.md`/architecture originally left the MCMCAN module/node as
"TBD". The AURIX Studio iLLD `IfxCan_PinMap_TC37x_LQFP176` (LQFP-176 =
TC375 package) defines:
- TX `IfxCan_TXD02_P15_0_OUT` (P15.0, alt5)
- RX `IfxCan_RXD02A_P15_1_IN` (P15.1, RxSel_a)

Low bus stays **CAN0 Node 0** (`P20.8` alt5 / `P20.7` RxSel_b), matching the
on-board TLE9251VSJ.

**Effect:** `architecture.md` §9.1, `wiring.md`, `board/board_pins.h`.

## 2026 — Corrected the "TC375 has no lockstep" error

**Correction:** The exact part `SAK-TC375TP-96F300W AA` has **CPU0 and CPU1
lockstep-protected; CPU2 is not**. Earlier text claimed "TC375 has no
lockstep" (true for some TC3xx family members, not this part).

**Effect:** `architecture.md` §6, §7, §11 — CPU1 chosen as the ASIL core.

## 2026 — Removed wrong WDT pin (P20.9/X1-28)

**Correction:** The earlier `WDT_WDI = P20.9 (X1-28)` was wrong — X1-28 is
`P20.14`; `P20.9` is package pin 127 (optional-flash INT path), not on X1.

**Decision:** `WDT_WDI = P33.1 / X2-29` (TPS3850-Q1), `DESIGN-SELECTED`.
Removed the fixed "100 Hz" claim (TPS3850-Q1 is a window watchdog; timing is
config-dependent). Watchdog part renamed `TPS3850` → `TPS3850-Q1`.

## 2026 — CAN_HIGH transceiver left as TBD (requirements only)

**Decision:** Do **not** pick `SN65HVD230`/`TLE9251V` as interchangeable.
Record requirements (ISO 11898-2, 500 kbit/s, 3.3 V TC375 I/O,
automotive-qualified preferred, defined standby, configurable termination)
and freeze the schematic after part selection.

## 2026 — Runtime mechanism is target-gated

**Decision:** The TC375 runtime model (3 AMP FreeRTOS kernels vs 1–2 kernels +
cyclic executors vs cyclic-only) is **not** pre-committed. The host implements
a **deterministic three-domain simulation** (CPU0/1/2 executors), not
FreeRTOS task shells. The runtime is chosen at the Phase D4 gate.

**Why:** *Do not port the ESP32 execution architecture; port its behavior.*
The execution architecture belongs to the TC375 and stays unresolved until
target bring-up.

## 2026 — Host-first validation strategy

**Decision:** Implement all scheduler-independent control/safety/protocol/FSM
logic as platform-agnostic C++ (`src/`), validated on the host with
differential tests (kinematics vs the original `rt-esp32` physics_model) and
a deterministic simulator with fault injection. No legacy ESP32 HAL-shadow
trick — the host implements `rta::hal` directly over `VirtualCanBus`.

## 2026 — AURIX toolchain chosen

**Decision:** HighTec **`tricore-gcc11`** (bundled in AURIX Studio) is the
build compiler (`-mcpu=tc38xx`, C++17). TASKING is available as an
alternative. `DEVICE_TC37X` (not `-mcpu`) selects the TC37x device.

## Open items

| Item | Status |
|------|--------|
| DAS driver install | Pending (manual UAC). |
| CAN_HIGH transceiver part | TBD (requirements in architecture §9.1.2). |
| On-hardware CAN loopback | Pending (D0). |
| Runtime model (D4) | Decided after target measurement. |
| WDI window timing (TPS3850-Q1) | Config-dependent. |
