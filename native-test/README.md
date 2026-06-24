# Native Firmware Testing Framework

Compiles E-Trike ECU firmware (C++/FreeRTOS) to run directly on a Windows or Linux PC — no ESP-IDF, no hardware. Real FreeRTOS preemptive scheduling, virtual CAN bus, simulated vehicle physics.

## Quick Start

```bash
# Configure (first time: downloads FreeRTOS kernel ~90s)
cmake -S native-test -B native-test/build -G "MinGW Makefiles" \
  -DCMAKE_C_COMPILER=C:/TDM-GCC-64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/TDM-GCC-64/bin/g++.exe

# Build
cmake --build native-test/build -j4

# Run tests
ctest --test-dir native-test/build --output-on-failure

# Run smoke test directly
./native-test/build/smoke_test.exe
```

## Requirements

- CMake >= 3.20
- GCC >= 8 (C++17) — tested with TDM-GCC 10.3.0
- Git (for FreeRTOS FetchContent)
- Internet access on first build (downloads FreeRTOS kernel)

## Architecture

```
native-test/
├── CMakeLists.txt          # Build: FreeRTOS FetchContent + targets
├── FreeRTOSConfig.h        # FreeRTOS config (1000 Hz tick, preemptive)
├── README.md
├── hal/
│   ├── shadow/             # ESP-IDF header stubs (include-path shadowing)
│   │   └── driver/
│   │       ├── twai.h      # TWAI → VirtualCanBus
│   │       ├── gpio.h      # Virtual pin array
│   │       ├── spi_master.h # No-op (MCP2515 replaced)
│   │       ├── i2c.h       # Virtual I2C
│   │       └── adc.h       # Configurable ADC values
│   │   ├── esp_log.h       # printf wrapper
│   │   ├── esp_timer.h     # std::chrono clock
│   │   ├── esp_err.h       # Error types
│   │   └── esp_heap_caps.h # Heap stub
│   ├── twai_stubs.cpp      # TWAI → VirtualCanBus routing
│   ├── gpio_stubs.cpp      # Virtual GPIO pin array
│   ├── i2c_stubs.cpp       # Virtual I2C bus
│   ├── adc_stubs.cpp       # Configurable ADC values
│   └── esp_timer_stubs.cpp # Clock implementation
├── can/
│   ├── virtual_can_bus.h   # Thread-safe CAN bus with fault injection
│   └── virtual_can_bus.cpp
├── ecus/                   # Per-ECU build modules (Phase 3+)
├── physics/                # Vehicle dynamics model (Phase 5+)
├── sim/                    # Peripheral simulators (Phase 5+)
└── test/
    ├── smoke_test.cpp      # Phase 1: FreeRTOS kernel verification
    └── scenarios/          # Integration scenarios (Phase 6+)
```

## Design Decisions

1. **Include-path shadowing** — HAL stubs replace ESP-IDF headers without modifying firmware source.
2. **Single FreeRTOS instance** — All ECU tasks run in one scheduler. True concurrency testing.
3. **CMake FetchContent** — FreeRTOS kernel auto-downloaded at build time.
4. **Win32/MinGW port** — Native Windows threads, no POSIX headers needed.

## Port Details

| Platform | FreeRTOS Port | Threads | Tick |
|----------|--------------|---------|------|
| Windows (MinGW) | MSVC-MingW | Win32 CreateThread/SuspendThread/ResumeThread | Timer thread + winmm |
| Linux | GCC/Posix | pthreads + signals | Timer thread + SIGALRM |

### MinGW Compatibility

The MSVC-MingW port expects `<timeapi.h>` from the Windows SDK. MinGW provides the same functions in `<mmsystem.h>`. The shim at `hal/shadow/timeapi.h` bridges this.

## Phases

| Phase | Status | What |
|-------|--------|------|
| 1. Foundation | ✅ Done | FreeRTOS kernel + HAL stubs + virtual CAN + smoke test |
| 2. Existing tests | ⬜ | Port g++ host tests to CMake/ctest |
| 3. RT ECU | ⬜ | Compile RT firmware natively with 8 FreeRTOS tasks |
| 4. SYS ECU | ⬜ | Compile SYS firmware, dual-ECU CAN routing |
| 5. Physics | ⬜ | Vehicle dynamics model + peripheral simulators |
| 6. Scenarios | ⬜ | 4 integration scenarios |
| 7. CI | ⬜ | GitHub Actions workflow |
