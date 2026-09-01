# RT-AURIX-Lite Board — Target Bring-up (Phase D)

**Board:** Infineon **KIT_A2G_TC375_LITE** (AURIX™ lite Kit V2, Rev 2.2)  
**MCU:** **SAK-TC375TP-96F300W AA**, LQFP-176, 3×300 MHz TriCore (CPU0/CPU1 lockstep, CPU2 not)

This directory holds the target board-facing code and the Phase D bring-up
procedure. The portable firmware logic is in [`../src/`](../src/) (host-validated).
The runtime mechanism (AMP FreeRTOS vs cyclic executors) is **decided at the
D4 gate** — no task shells are written before that.

---

## Toolchain (verified present)

| Component | Path |
|---|---|
| AURIX Studio 1.10.36 | `E:\Infineon\AURIX-Studio-1.10.36\AURIX-studio.exe` |
| HighTec GNU TriCore GCC 11.3.1 | `...\tools\Compilers\tricore-gcc11\bin\tricore-elf-gcc.exe` |
| TASKING 1.1r8 | `...\tools\Compilers\Tasking_1.1r8\ctc\bin\cctc.exe` |
| iLLD 1.20.0 (TC37A) | `...\build_system\bundled-artefacts-repo\project-initializer\tricore-tc3xx\1.17-18\iLLDs\Full_Set\iLLD_1_20_0__TC37A.zip` |
| Device support | `KIT_A2G_TC375_LITE → TC375LK` template, `TC37xTP_A-Step → TC37A` device |

**Compiler default:** HighTec `tricore-gcc11` (matches the repo's GNU/C++17 style).

---

## Prerequisite: install DAS driver

The on-board miniWiggler (USB X4) and the 10-pin DAP debug path require the
**Infineon DAS** driver. AURIX Studio needs DAS as its debug-driver layer, but
DAS is a **separate Windows driver install** (not part of the IDE itself).

**Where it is:** `E:\Infineon\AURIX-Studio-1.10.36\DAS_V8_4_0_SETUP.exe` (40 MB, bundled).

**How to install (manual, needs admin/UAC):**
1. In Windows Explorer, double-click `DAS_V8_4_0_SETUP.exe`.
2. Approve the **User Account Control (UAC)** prompt.
3. Click through the installer (defaults are fine).

> It cannot be installed silently from a non-elevated shell. AURIX Studio
> *uses* DAS once installed — you don't run DAS separately.

After install, the miniWiggler (LED5 ACT) works for debug/flashing.

---

## Create the ADS project (D0)

Use the AURIX Studio new-project wizard with the TC375 Lite Kit target:

1. Launch `AURIX-studio.exe`.
2. **File → New → AURIX Project**.
3. Device: **TC375** (`TC37xTP_A-Step`), Board: **KIT_A2G_TC375_LITE** (→ `TC375LK`).
4. iLLD set: **Full_Set** (extracts `iLLD_1_20_0__TC37A`).
5. Compiler: **HighTec tricore-gcc11**.
6. The wizard generates the 3-core scaffold (Cpu0/1/2 mains, linker
   `Lcf_Gnuc_Tricore_Tc.lsl`, BMHD, `Ifx_Cfg_Ssw*`).

Then add the portable core + board HAL sources:

- `../src/` (rta_core logic, header-only domain/app/protocol/ipc + `.cpp`).
- `board/hal_aurix/aurix_hal.{h,cpp}` (board HAL).
- `board/main.cpp` (target entry).

> The iLLD is large and IDE-generated; keep it out of git (see `.gitignore`).

---

## Bring-up checklist

### D0 — Walking skeleton
- [ ] DAS installed; board powers via X4 (LED4 green) and/or X3.
- [ ] AURIX Studio project builds (HighTec) with a blinky on LED1/LED2.
- [ ] miniWiggler debug session connects (LED5 ACT on).
- [x] `AurixClock::init()` (STM) + `AurixGpio::init()` (pins) implemented.
- [x] `AurixCan::init()` (CAN0 Node 0 + Node 2, 500 kbit/s) implemented.
- [ ] Actually run the skeleton on hardware and verify a CAN0 loopback.

### D1 — Multicore startup
- [ ] CPU0 boots; CPU1 and CPU2 brought up (template Cpu1/Cpu2 mains).
- [ ] Verify per-core execution (lockstep CPU0/CPU1, non-lockstep CPU2).

### D2 — Real CAN_LOW / CAN_HIGH
- [ ] **CAN_LOW:** CAN0 Node 0 (`P20.8` alt5 TX, `P20.7` RxSel_b RX),
      on-board TLE9251VSJ, `P20.6` standby low. 500 kbit/s loopback.
- [ ] **CAN_HIGH:** CAN0 Node 2 (`P15.0` alt5 TX, `P15.1` RxSel_a RX),
      external transceiver (part TBD, architecture §9.1.2) on mikroBUS 13/14.
      Verify TX/RX + termination.

### D3 — LMU + DSYNC + SRI + MPU/SMU
- [ ] Shared-memory placement (LMU/DLMU), alignment, publication ordering.
- [ ] DSYNC/compiler barriers in `rta::ipc` target implementation.
- [ ] SRI service requests, MPU/BMP access, SMU safety handling.

### D4 — Runtime decision gate
- [ ] Candidates: 3 AMP FreeRTOS kernels; 1–2 kernels + cyclic executors;
      cyclic executors only. Choose from measured WCET/latency/contention.
- [ ] Document the decision in `../architecture.md` §6.2.

### D5 — Production runtime shells
- [ ] Wrap the host-validated `rta::` controllers in the chosen runtime
      (e.g. `cpu1_10ms_tick()` → `motion.control(...)`).

---

## Files

| File | Purpose |
|---|---|
| `board_pins.h` | Board pin map (CAN, GPIO, WDI) — frozen from iLLD pinmap. |
| `hal_aurix/aurix_hal.h` | Board HAL interfaces over iLLD (init bodies filled at D0/D2). |
| `main.cpp` | Target entry wiring rta:: controllers to the board HAL. |

See [`../work-plan.md`](../work-plan.md) for the full phase plan.
