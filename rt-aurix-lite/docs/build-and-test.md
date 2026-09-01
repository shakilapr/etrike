# RT-AURIX-Lite — Build & Test

How to build and test the firmware, both on the host and headlessly for the
AURIX target.

## 1. Host validation (native-test)

The portable core (`src/`) is validated in the existing **`native-test`**
CMake harness (FreeRTOS host port + virtual CAN). It is wired in via
`add_subdirectory` from `native-test/CMakeLists.txt`.

```bash
# Configure (from repo root)
cmake -S native-test -B native-test/build -G "MinGW Makefiles" \
      -DCMAKE_C_COMPILER=C:/TDM-GCC-64/bin/gcc.exe \
      -DCMAKE_CXX_COMPILER=C:/TDM-GCC-64/bin/g++.exe

# Build + run all tests (including rta_*)
cmake --build native-test/build -j4
ctest --test-dir native-test/build --output-on-failure
```

The `rta_*` targets:

```bash
ctest --test-dir native-test/build -R "rta_"
```

> Requires the repo's FreeRTOS FetchContent + the shared `protocol/`
> (generated subset + `protocol/codecs`). The portable core has **no
> FreeRTOS/iLLD dependency**; `rta_core` links only `rta_headers`.

## 2. Headless AURIX build (toolchain proof)

The board target compiles headlessly with the AURIX Studio HighTec
`tricore-gcc11` — no Eclipse wizard required.

```powershell
# From repo root
pwsh rt-aurix-lite/board/port/build.ps1
```

Artifacts (gitignored) → `rt-aurix-lite/board/port/build/`:

- `d0_smoke.elf` — C proof: TC37x iLLD SFR + LQFP-176 CAN pin tables link.
- `kinematics.o` … `controllers.o` — portable core compiled for TC375.
- `aurix_hal.o` — board HAL over iLLD.

Include roots mirror what AURIX Studio passes (iLLD `TC3xx/Tricore`,
`Infra/Sfr/TC37x`, `Infra/Platform`, `Service/CpuGeneric`, `_PinMap`,
`Configurations/` for `Ifx_Cfg.h`).

> `-mcpu=tc38xx`: the HighTec multilib covers the TC3xx ISA; the TC375
> device is selected by `DEVICE_TC37X` in `board/port/Configurations/Ifx_Cfg.h`
> (and `board/Configurations/Ifx_Cfg.h`), not by `-mcpu`.

## 3. AURIX Studio build

Import `rt-aurix-lite/board` (File → Import → Existing Projects) and build
the **TriCore Debug (GCC)** config. See [`target-bringup.md`](target-bringup.md).

## Prerequisites

- AURIX Studio 1.10.36 (HighTec `tricore-gcc11` + TASKING), iLLD extracted to
  `board/Libraries/`.
- DAS driver installed for flashing/debug (see [`target-bringup.md`](target-bringup.md)).
