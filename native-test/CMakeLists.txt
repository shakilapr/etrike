cmake_minimum_required(VERSION 3.20)
project(etrike-native-test VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ETRIKE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(protocol_headers INTERFACE)
target_include_directories(protocol_headers INTERFACE ${ETRIKE_ROOT})

# Static linking — avoids runtime DLL dependencies (libgcc_s_seh-1.dll etc.)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

# Use TDM-GCC on Windows if available
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0)
        message(WARNING "GCC ${CMAKE_CXX_COMPILER_VERSION} is old. "
                        "Consider using C:/TDM-GCC-64/bin/g++.exe (GCC 10.3)")
    endif()
endif()

# ── FreeRTOS via FetchContent ────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    freertos_kernel
    GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
    GIT_TAG        V11.1.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  ""
)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)  # Allow FetchContent_Populate with declared details
endif()
FetchContent_GetProperties(freertos_kernel)
if(NOT freertos_kernel_POPULATED)
    FetchContent_Populate(freertos_kernel)
endif()

# Port selection
if(WIN32)
    # MSVC-MingW port: native Windows threads (no POSIX headers needed)
    set(FREERTOS_PORT_DIR ${freertos_kernel_SOURCE_DIR}/portable/MSVC-MingW)
    set(FREERTOS_PORT_SOURCES
        ${FREERTOS_PORT_DIR}/port.c
    )
    set(FREERTOS_EXTRA_LIBS)
else()
    # POSIX port for Linux/macOS
    set(FREERTOS_PORT_DIR ${freertos_kernel_SOURCE_DIR}/portable/ThirdParty/GCC/Posix)
    set(FREERTOS_PORT_SOURCES
        ${FREERTOS_PORT_DIR}/port.c
        ${FREERTOS_PORT_DIR}/utils/wait_for_event.c
    )
    find_package(Threads REQUIRED)
    set(FREERTOS_EXTRA_LIBS Threads::Threads)
endif()

# Core kernel
set(FREERTOS_CORE
    ${freertos_kernel_SOURCE_DIR}/tasks.c
    ${freertos_kernel_SOURCE_DIR}/queue.c
    ${freertos_kernel_SOURCE_DIR}/list.c
    ${freertos_kernel_SOURCE_DIR}/timers.c
    ${freertos_kernel_SOURCE_DIR}/event_groups.c
)

# Heap (heap_4 — best general-purpose choice)
set(FREERTOS_HEAP
    ${freertos_kernel_SOURCE_DIR}/portable/MemMang/heap_4.c
)

add_library(freertos STATIC
    ${FREERTOS_CORE}
    ${FREERTOS_PORT_SOURCES}
    ${FREERTOS_HEAP}
)

target_include_directories(freertos PUBLIC
    ${freertos_kernel_SOURCE_DIR}/include
    ${FREERTOS_PORT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}          # FreeRTOSConfig.h
)

# MinGW shims (timeapi.h → mmsystem.h, etc.)
if(WIN32)
    target_include_directories(freertos PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    )
    target_compile_definitions(freertos PRIVATE
        _WIN32_WINNT=0x0601   # Windows 7+ for CRITICAL_SECTION APIs
    )
    # Pre-include our self-contained timeapi shim for all freertos units.
    # Provides TIMECAPS/timeGetDevCaps/timeBeginPeriod/timeEndPeriod
    # without requiring windows.h or mmsystem.h (GCC 14+ compatible).
    target_compile_options(freertos BEFORE PRIVATE
        -include "${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow/timeapi_win32.h"
    )
endif()

target_compile_definitions(freertos PRIVATE PROJCODELOC=1)

# Windows ports need winmm for timeBeginPeriod/timeEndPeriod. CMake adds the
# correct platform-specific library syntax for both MSVC and MinGW.
if(WIN32)
    target_link_libraries(freertos PUBLIC winmm)
endif()

if(FREERTOS_EXTRA_LIBS)
    target_link_libraries(freertos PUBLIC ${FREERTOS_EXTRA_LIBS})
endif()

# ── HAL stub library (include shadowing + implementation) ────────────
add_library(hal_stubs STATIC
    hal/twai_stubs.cpp
    hal/gpio_stubs.cpp
    hal/i2c_stubs.cpp
    hal/adc_stubs.cpp
    hal/esp_timer_stubs.cpp
)
target_include_directories(hal_stubs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_CURRENT_SOURCE_DIR}              # for can/virtual_can_bus.h
    ${freertos_kernel_SOURCE_DIR}/include    # for real FreeRTOS headers
)
target_link_libraries(hal_stubs PUBLIC freertos virtual_can protocol_headers)

# ── Virtual CAN bus library ──────────────────────────────────────────
add_library(virtual_can STATIC
    can/virtual_can_bus.cpp
)
target_include_directories(virtual_can PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(virtual_can PUBLIC freertos protocol_headers)

# ── Smoke test ───────────────────────────────────────────────────────
add_executable(smoke_test
    test/smoke_test.cpp
)
target_link_libraries(smoke_test PRIVATE freertos hal_stubs virtual_can)
target_include_directories(smoke_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
)

# ── Canonical root protocol tests ───────────────────────────────────
add_executable(protocol_core_custom_supervision
    ${ETRIKE_ROOT}/protocol/tests/cpp/test_protocol.cpp
)
target_link_libraries(protocol_core_custom_supervision PRIVATE protocol_headers)

add_executable(protocol_generated_vectors
    ${ETRIKE_ROOT}/protocol/tests/cpp/test_generated_vectors.cpp
)
target_link_libraries(protocol_generated_vectors PRIVATE protocol_headers)

add_executable(protocol_compat
    ${ETRIKE_ROOT}/protocol/tests/cpp/test_compat.cpp
)
target_link_libraries(protocol_compat PRIVATE protocol_headers)

add_executable(protocol_compat_cpp11
    ${ETRIKE_ROOT}/protocol/tests/cpp/test_compat_cpp11.cpp
)
set_property(TARGET protocol_compat_cpp11 PROPERTY CXX_STANDARD 11)
target_link_libraries(protocol_compat_cpp11 PRIVATE protocol_headers)

# ── MCP2515 CNF config test ──────────────────────────────────────────
add_executable(mcp2515_config
    test/test_mcp2515_config.cpp
)
target_include_directories(mcp2515_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRIKE_ROOT}
    ${ETRIKE_ROOT}/shared
    ${ETRIKE_ROOT}/rt-esp32/src
)
# The driver needs RT configuration headers, but no FreeRTOS or HAL library.

# ── RT CAN router test ──────────────────────────────────────────────
add_executable(rt_can_rx_router
    test/test_rt_can_rx_router.cpp
)
target_link_libraries(rt_can_rx_router PRIVATE freertos hal_stubs virtual_can)
target_include_directories(rt_can_rx_router PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRIKE_ROOT}
    ${ETRIKE_ROOT}/shared
    ${ETRIKE_ROOT}/rt-esp32/src
)

# ── RT CAN dispatch test ─────────────────────────────────────────────
add_executable(rt_can_dispatch
    test/test_rt_can_dispatch.cpp
    ${ETRIKE_ROOT}/rt-esp32/src/physics_model.cpp
)
target_link_libraries(rt_can_dispatch PRIVATE freertos hal_stubs virtual_can)
target_include_directories(rt_can_dispatch PRIVATE
    ${freertos_kernel_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRIKE_ROOT}
    ${ETRIKE_ROOT}/shared
    ${ETRIKE_ROOT}/rt-esp32/src
)

# ── RT safety monitor test ──────────────────────────────────────────
add_executable(rt_safety_monitor
    test/test_rt_safety_monitor.cpp
    ${ETRIKE_ROOT}/rt-esp32/src/physics_model.cpp
)
target_link_libraries(rt_safety_monitor PRIVATE freertos hal_stubs virtual_can)
target_include_directories(rt_safety_monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRIKE_ROOT}
    ${ETRIKE_ROOT}/shared
    ${ETRIKE_ROOT}/rt-esp32/src
)

# ── SYS CAN dispatch test ────────────────────────────────────────────
add_executable(sys_can_dispatch
    test/test_sys_can_dispatch.cpp
)
target_include_directories(sys_can_dispatch PRIVATE
    ${ETRIKE_ROOT}
)

# ── Task watchdog test ───────────────────────────────────────────────
add_executable(task_watchdog
    test/test_task_watchdog.cpp
)

# ── Heartbeat recovery test ──────────────────────────────────────────
add_executable(heartbeat_recovery
    test/test_heartbeat_recovery.cpp
)

# ── Watchdog wraparound test ─────────────────────────────────────────
add_executable(watchdog_wraparound
    test/test_watchdog_wraparound.cpp
)

# ── Dual heartbeat independence test ─────────────────────────────────
add_executable(dual_heartbeat
    test/test_dual_heartbeat.cpp
)

# ── ESTOP latch test (PCR3) ──────────────────────────────────────────
add_executable(estop_latch
    test/test_estop_latch.cpp
)

# ── SPI failure propagation test (PCR2) ─────────────────────────────
add_executable(spi_failure
    test/test_spi_failure.cpp
)

# ── Component, safety, and generated contract checks ───────────────
add_executable(components test/test_components.cpp)
add_executable(component_io test/test_component_io.cpp)
target_link_libraries(component_io PRIVATE protocol_headers)
add_executable(safety_features test/test_safety_features.cpp)
target_link_libraries(safety_features PRIVATE protocol_headers)
add_executable(signal_chains test/test_signal_chains.cpp)
target_link_libraries(signal_chains PRIVATE protocol_headers)
add_executable(all_signals test/test_all_signals.cpp)

add_executable(dlc_consistency test/test_dlc_consistency.cpp)
target_link_libraries(dlc_consistency PRIVATE protocol_headers)

add_executable(gateway_forwarding test/test_gateway_forwarding.cpp)
target_link_libraries(gateway_forwarding PRIVATE protocol_headers)
target_link_libraries(dual_heartbeat PRIVATE protocol_headers)
target_include_directories(dual_heartbeat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ETRIKE_ROOT}
    ${ETRIKE_ROOT}/shared
    ${ETRIKE_ROOT}/rt-esp32/src
)

# ── CTest ────────────────────────────────────────────────────────────
enable_testing()
add_test(NAME smoke               COMMAND smoke_test)
add_test(NAME protocol_core_custom_supervision COMMAND protocol_core_custom_supervision)
add_test(NAME protocol_generated_vectors COMMAND protocol_generated_vectors)
add_test(NAME protocol_compat      COMMAND protocol_compat)
add_test(NAME protocol_compat_cpp11 COMMAND protocol_compat_cpp11)
add_test(NAME mcp2515_config      COMMAND mcp2515_config)
add_test(NAME rt_can_rx_router    COMMAND rt_can_rx_router)
add_test(NAME rt_can_dispatch     COMMAND rt_can_dispatch)
add_test(NAME sys_can_dispatch    COMMAND sys_can_dispatch)
add_test(NAME task_watchdog       COMMAND task_watchdog)
add_test(NAME rt_safety_monitor   COMMAND rt_safety_monitor)
add_test(NAME heartbeat_recovery  COMMAND heartbeat_recovery)
add_test(NAME watchdog_wraparound COMMAND watchdog_wraparound)
add_test(NAME dual_heartbeat      COMMAND dual_heartbeat)
add_test(NAME estop_latch          COMMAND estop_latch)
add_test(NAME spi_failure          COMMAND spi_failure)
add_test(NAME components           COMMAND components)
add_test(NAME component_io         COMMAND component_io)
add_test(NAME safety_features      COMMAND safety_features)
add_test(NAME signal_chains        COMMAND signal_chains)
add_test(NAME all_signals          COMMAND all_signals)
add_test(NAME dlc_consistency      COMMAND dlc_consistency)
add_test(NAME gateway_forwarding   COMMAND gateway_forwarding)

# ── Simulation engine native (IPC via stdin/stdout, no FreeRTOS) ──────
add_executable(sim_engine_native
    sim-engine/main_native.cpp
    ${ETRIKE_ROOT}/rt-esp32/src/physics_model.cpp
)
target_include_directories(sim_engine_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sim-engine
    ${ETRIKE_ROOT}
    ${ETRIKE_ROOT}/shared
    ${ETRIKE_ROOT}/rt-esp32/src
    ${ETRIKE_ROOT}/sys-esp32/src
    ${ETRIKE_ROOT}/mtr-stm32/src
)
target_compile_definitions(sim_engine_native PRIVATE
    CONFIG_BYPASS_EPS_C_SYNC
    CONFIG_BENCH_SOLO
)
# Link with -static to avoid DLL dependencies
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(sim_engine_native PRIVATE -static)
endif()

# ── Status ───────────────────────────────────────────────────────────
message(STATUS "Native test build configured.")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  FreeRTOS port: POSIX (${FREERTOS_PORT_DIR})")
