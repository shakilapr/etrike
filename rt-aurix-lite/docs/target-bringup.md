# RT-AURIX-Lite — Target Bring-up (Phase D)

Bringing up the **KIT_A2G_TC375_LITE** board with the consolidated firmware.

## Prerequisites

### 1. AURIX Studio (verified installed)

- **Path:** `E:\Infineon\AURIX-Studio-1.10.36\AURIX-studio.exe`
- **Compilers:** HighTec `tricore-gcc11` (11.3.1), TASKING 1.1r8
- **iLLD 1.20.0 (TC37A):** bundled zip; extracted to `board/Libraries/`
- **Device support:** `KIT_A2G_TC375_LITE → TC375LK`, `TC37xTP_A-Step → TC37A`

### 2. DAS driver (must install — admin/UAC)

AURIX Studio uses **DAS** as the debug-driver layer for the on-board
miniWiggler / DAP. DAS is a **separate Windows driver install**, not part of
the IDE itself.

- **Where:** `E:\Infineon\AURIX-Studio-1.10.36\DAS_V8_4_0_SETUP.exe`
- **Install:** double-click → approve UAC → accept defaults.
- Cannot be run silently from a non-elevated shell.

### 3. Board hardware

- AURIX Lite Kit V2 (TC375), micro-USB cable (X4) and/or DC supply (X3).
- For CAN_HIGH (D2): an external HS-CAN transceiver (part TBD, see
  `architecture.md` §9.1.2) on mikroBUS 13/14.

## Open the project in AURIX Studio

**Option A — import (recommended):** File → Import → Existing Projects into
Workspace → root = `E:\work\etrike\rt-aurix-lite\board`.

**Option B — new-project wizard (fallback):** File → New → AURIX Project at
the same folder; device TC375, board KIT_A2G_TC375_LITE, iLLD Full Set,
compiler HighTec tricore-gcc11.

Build config: **TriCore Debug (GCC)**.

## Bring-up checklist

### D0 — Walking skeleton
- [ ] DAS installed; board powers (LED4 green).
- [ ] Import `board/` and build (HighTec).
- [ ] miniWiggler debug session connects (LED5 ACT on).
- [x] `AurixClock::init()` / `AurixGpio::init()` / `AurixCan::init()`
      implemented (CAN0 Node 0 + Node 2, 500 kbit/s).
- [ ] Run on hardware, verify CAN0 loopback.

### D1 — Multicore startup
- [ ] CPU0 boots; CPU1/CPU2 brought up (`Cpu1_Main.c`, `Cpu2_Main.c`).
- [ ] Verify per-core execution (CPU0/CPU1 lockstep, CPU2 non-lockstep).

### D2 — Real CAN_LOW / CAN_HIGH
- [ ] CAN_LOW: CAN0 Node 0 (P20.8 alt5 TX, P20.7 RxSel_b RX), on-board
      TLE9251VSJ, P20.6 STB low. 500 kbit/s loopback.
- [ ] CAN_HIGH: CAN0 Node 2 (P15.0 alt5 TX, P15.1 RxSel_a RX), external
      transceiver on mikroBUS 13/14. Verify TX/RX + termination.

### D3 — LMU + DSYNC + SRI + MPU/SMU
- [ ] Shared-memory placement (LMU/DLMU), alignment, publication ordering.
- [ ] DSYNC/compiler barriers in the `rta::ipc` target implementation.
- [ ] SRI service requests, MPU/BMP access, SMU safety handling.

### D4 — Runtime decision gate
- [ ] Choose between 3 AMP FreeRTOS kernels / 1–2 kernels + cyclic executors /
      cyclic executors only, based on measured WCET/latency/contention.
- [ ] Document the decision in `architecture.md` §6.2.

### D5 — Production runtime shells
- [ ] Wrap the host-validated `rta::` controllers in the chosen runtime
      (e.g. `cpu1_10ms_tick()` → `motion.control(...)`).

## Key frozen facts

| Item | Value |
|------|-------|
| CAN_LOW | CAN0 Node 0 (P20.8 TX, P20.7 RX) |
| CAN_HIGH | CAN0 Node 2 (P15.0 TX, P15.1 RX) |
| Device | `SAK-TC375TP-96F300W AA`, LQFP-176, 3×300 MHz |
| Lockstep | CPU0/CPU1 (CPU2 non-lockstep) |
| Watchdog | TPS3850-Q1, WDI P33.1 |
