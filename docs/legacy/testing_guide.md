# Testing Guide

This document outlines how to run the various test suites for the eTrike project. Our testing strategy spans three major environments: the Node.js simulation, Real-Time/System ESP32 hardware/native compilation, and CMake native C++ testing.

## 1. Automated Software Gate (All Tests)

The easiest way to run the entire test suite and validate all code against our CI rules is to use the PowerShell gate script.

```powershell
# From the project root
powershell -File tools/phase1-software-gate.ps1
```
This script will:
- Clean build matrices
- Compile Native C++ Tests
- Compile Vehicle Targets for `rt-esp32` and `sys-esp32`
- Run Unity tests via PlatformIO for `rt-esp32` and `sys-esp32` natively
- Run Node.js simulation tests via `npm`
- Validate protocol drift and schema alignment

## 2. Running Node.js Simulation Tests

The simulation tests validate safety invariants, CAN behavior mimics, boundary math, and property/fuzz scenarios.

```bash
cd simulation
npm install   # (only required initially or when package.json changes)
npm run test
```
*Note: This runs tests using `vitest`. You can use `npm run test -- --coverage` to generate a coverage report.*

## 3. Running ESP32 Native Unity Tests (PlatformIO)

We use PlatformIO's Unity test framework to run isolated module testing for the ESP32 codebases directly on your host machine (Native).

**For Real-Time ESP32:**
```bash
cd rt-esp32
pio test -e native
```

**For System ESP32:**
```bash
cd sys-esp32
pio test -e native
```

## 4. Running Integration C++ Tests (CMake)

The `native-test` folder contains mock environments and cross-communication tests.

```bash
cd native-test
cmake -B build
cmake --build build
cd build
ctest --output-on-failure
```

## Common Issues
- **[WinError 32] File Locks**: On Windows, switching between PlatformIO environments (`native` vs `vehicle`) can occasionally trigger filesystem locks while deleting `.pio/build`. If this occurs, you can manually delete the `.pio` folder or use `powershell -File tools/phase1/clean_build_matrix.ps1`.
