# E-Trike Test Running Guide

How to run every test suite in the project.

---

## Quick Reference

| Suite | Command | Location |
|-------|---------|----------|
| Native C++ | `g++` + run | `native-test/test/` |
| Simulation | `npm test` | `simulation/` |
| Backend | `npm test` | `debug-tool/backend/` |
| UI | `npm test` | `debug-tool/ui/` |
| DLC Generator | `python test_dlc_generator.py` | `native-test/test/` |
| Firmware Compile | `pio run -e vehicle` | each ECU directory |

---

## 1. Native C++ Tests

No build system required — compile and run directly with g++.

```powershell
cd native-test\test

# PCR3: ESTOP latch state machine
g++ -std=c++17 -o estop_latch.exe test_estop_latch.cpp
.\estop_latch.exe

# PCR2: SPI failure propagation
g++ -std=c++17 -o spi_failure.exe test_spi_failure.cpp
.\spi_failure.exe

# Heartbeat recovery (timeout -> recovery)
g++ -std=c++17 -o hb_recovery.exe test_heartbeat_recovery.cpp
.\hb_recovery.exe

# Watchdog wraparound (uint32 overflow)
g++ -std=c++17 -o wd_wrap.exe test_watchdog_wraparound.cpp
.\wd_wrap.exe

# Dual heartbeat independence (low/high bus counters)
g++ -std=c++17 -I..\..\shared -I..\..\shared\can -I..\..\rt-esp32\src -o dual_hb.exe test_dual_heartbeat.cpp
.\dual_hb.exe
```

Or compile all at once with CMake (requires FreeRTOS kernel submodule):
```powershell
cd native-test
mkdir build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

**Expected**: All tests pass with 0 failures.

---

## 2. Simulation Tests (TypeScript/Vitest)

```powershell
cd simulation
npm install        # first time only
npm test           # run all 350+ tests
```

To run a single test file:
```powershell
npx vitest run tests/unit/can-encoding.test.ts
npx vitest run tests/integration/bus-routing.test.ts
```

To run with watch mode (re-run on file change):
```powershell
npx vitest
```

**Expected**: ~350 tests pass, 0 fail. A few tests skipped (placeholders).

---

## 3. Backend Tests (TypeScript/Vitest)

```powershell
cd debug-tool\backend
npm install        # first time only
npm test           # run all 171 tests
```

Key test files:
- `src/types/can.test.ts` — CAN frame decode/encode (128 tests)
- `src/test-mcp2515.test.ts` — MCP2515 configuration and signal tests

**Expected**: 171 tests pass, 0 fail.

---

## 4. UI Tests (Vitest + jsdom)

```powershell
cd debug-tool\ui
npm install        # first time only
npm test           # requires jsdom environment
```

**Note**: UI tests require jsdom. Install with `npm install` if missing.

---

## 5. DLC Generator Test (Python)

```powershell
cd native-test\test
python test_dlc_generator.py
```

Verifies generator output consistency against current YAML definitions.

**Expected**: All checks pass.

---

## 6. Firmware Compilation (PlatformIO)

Each ECU must compile without errors with the vehicle profile.

```powershell
# RT ESP32-S3 — realtime physics + CAN gateway
cd rt-esp32
pio run -e vehicle

# SYS ESP32-S3 — safety + body control
cd sys-esp32
pio run -e vehicle

# MTR STM32 — motor actuation (EGAS Level 1)
cd mtr-stm32
pio run -e vehicle

# PWT ESP32-S3 — powertrain gateway
cd pwt-esp32
pio run -e vehicle
```

To compile the bench profile (with bypass flags):
```powershell
pio run -e bench
```

**Expected**: All 4 ECUs compile with 0 errors. Warnings OK.

---

## 7. Full Test Run (All Suites)

```powershell
# 1. Native C++ tests
cd native-test\test
g++ -std=c++17 -o estop_latch.exe test_estop_latch.cpp && .\estop_latch.exe
g++ -std=c++17 -o spi_failure.exe test_spi_failure.cpp && .\spi_failure.exe
g++ -std=c++17 -o hb_recovery.exe test_heartbeat_recovery.cpp && .\hb_recovery.exe
g++ -std=c++17 -o wd_wrap.exe test_watchdog_wraparound.cpp && .\wd_wrap.exe
g++ -std=c++17 -I..\..\shared -I..\..\shared\can -I..\..\rt-esp32\src -o dual_hb.exe test_dual_heartbeat.cpp && .\dual_hb.exe
python test_dlc_generator.py

# 2. Simulation tests
cd ..\..\simulation && npm test

# 3. Backend tests  
cd ..\debug-tool\backend && npm test

# 4. Firmware compilations
cd ..\..\rt-esp32 && pio run -e vehicle
cd ..\sys-esp32 && pio run -e vehicle
cd ..\mtr-stm32 && pio run -e vehicle
cd ..\pwt-esp32 && pio run -e vehicle
```

---

## Test Inventory

| Test File | Type | Tests | What It Verifies |
|-----------|------|-------|-----------------|
| `test_estop_latch.cpp` | Native C++ | 6 scenarios | ESTOP latch state machine (PCR3) |
| `test_spi_failure.cpp` | Native C++ | 6 scenarios | SPI failure propagation (PCR2) |
| `test_heartbeat_recovery.cpp` | Native C++ | 6 scenarios | Heartbeat timeout + recovery |
| `test_watchdog_wraparound.cpp` | Native C++ | 6 scenarios | Watchdog uint32 overflow |
| `test_dual_heartbeat.cpp` | Native C++ | 8 scenarios | Dual-bus heartbeat independence |
| `test_dlc_generator.py` | Python | 8 checks | Generator DLC consistency |
| `can.test.ts` | Vitest | 128 tests | CAN frame decode/encode |
| `test-mcp2515.test.ts` | Vitest | Multiple | MCP2515 config + signals |
| `bus-routing.test.ts` | Vitest | Bus routing | Correct bus assignment |
| `can-encoding.test.ts` | Vitest | Soak test | 18s content stability |
| Simulation suite | Vitest | ~350 tests | Full system simulation |
