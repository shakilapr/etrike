# RT-AURIX-Lite Board Port — D0 Headless Build

This directory proves the board target builds **headlessly** with the AURIX
Studio HighTec `tricore-gcc11` toolchain — no Eclipse wizard required.

## AURIX Studio compatibility (verified)

The portable firmware core (`src/`) and the board HAL are **AURIX-IDE
compatible**: they compile with `tricore-elf-gcc`/`tricore-elf-g++`
(HighTec 11.3.1) using `-mcpu=tc38xx` and C++17. The TC375 device is
selected by `DEVICE_TC37X` + `IFX_PIN_PACKAGE_LQFP176` (from
`Configurations/Ifx_Cfg.h`), which drives the iLLD `TC37x` SFR + LQFP-176
pin maps.

| What | Status |
|---|---|
| TC37x iLLD register headers + LQFP-176 pin maps | ✅ compile+link (`test_d0_smoke.c`) |
| Portable `rta::` core (domain/app/protocol/ipc) | ✅ compiles with `tricore-elf-g++` (C++17) |
| Portable HAL + protocol adapters | ✅ compiles (no iLLD dependency in `src/`) |
| DAS on-board debug | ⚠️ requires `DAS_V8_4_0_SETUP.exe` (admin/UAC) |

> `-mcpu=tc38xx`: the HighTec `tricore-gcc11` multilib covers the TC3xx ISA
> family; the TC37x device is selected by the `DEVICE_TC37X` macro, not
> `-mcpu`. The Eclipse managed build passes the same effective flags.

## Build

```powershell
pwsh board/port/build.ps1
```

Outputs (gitignored) to `board/port/build/`:

- `d0_smoke.elf` — C proof: iLLD SFR headers + CAN LQFP-176 pin tables link.
- `kinematics.o` … `controllers.o` — the portable `rta::` core compiled for TC375.

## Files

| File | Purpose |
|---|---|
| `build.ps1` | Headless build script (toolchain + iLLD include roots). |
| `Configurations/Ifx_Cfg.h` | Rendered device config: `DEVICE_TC37X`, `IFX_PIN_PACKAGE_LQFP176`, 20 MHz XTAL. |
| `test/test_d0_smoke.c` | C compile/link proof against TC37x iLLD SFR + pin map. |
| `build/` | Build output (gitignored). |

## CAN bindings (frozen)

From `IfxCan_PinMap_TC37x_LQFP176` (see `board/board_pins.h`):

| Bus | Module/Node | TX | RX |
|-----|-------------|----|----|
| CAN_LOW | CAN0 Node 0 | `IfxCan_TXD00_P20_8_OUT` | `IfxCan_RXD00B_P20_7_IN` |
| CAN_HIGH | CAN0 Node 2 | `IfxCan_TXD02_P15_0_OUT` | `IfxCan_RXD02A_P15_1_IN` |
