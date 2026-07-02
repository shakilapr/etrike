cmake_minimum_required(VERSION 3.20)
project(etrike-native-test VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
endif()

target_compile_definitions(freertos PRIVATE PROJCODELOC=1)

# MSVC-MingW port needs winmm.lib for timeBeginPeriod/timeEndPeriod
if(WIN32)
    target_link_libraries(freertos PUBLIC -lwinmm)
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
    ${CMAKE_SOURCE_DIR}/../shared            # for can/can_protocol.h
)
target_link_libraries(hal_stubs PUBLIC freertos virtual_can)

# ── Virtual CAN bus library ──────────────────────────────────────────
add_library(virtual_can STATIC
    can/virtual_can_bus.cpp
)
target_include_directories(virtual_can PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/../shared
)
target_link_libraries(virtual_can PUBLIC freertos)

# ── Smoke test ───────────────────────────────────────────────────────
add_executable(smoke_test
    test/smoke_test.cpp
)
target_link_libraries(smoke_test PRIVATE freertos hal_stubs virtual_can)
target_include_directories(smoke_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_SOURCE_DIR}/../shared
)

# ── Protocol roundtrip test (all structs with to_frame / from_frame) ──
add_executable(protocol_roundtrip
    test/test_protocol_roundtrip.cpp
)
target_link_libraries(protocol_roundtrip PRIVATE freertos hal_stubs virtual_can)
target_include_directories(protocol_roundtrip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_SOURCE_DIR}/../shared
)

# ── SYNTREE checksum full test ──────────────────────────────────────
add_executable(checksum_full
    test/test_checksum_full.cpp
)
target_link_libraries(checksum_full PRIVATE freertos hal_stubs virtual_can)
target_include_directories(checksum_full PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_SOURCE_DIR}/../shared
)

# ── MCP2515 CNF config test ──────────────────────────────────────────
add_executable(mcp2515_config
    test/test_mcp2515_config.cpp
)
target_include_directories(mcp2515_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_SOURCE_DIR}/../shared
    ${CMAKE_SOURCE_DIR}/../rt-esp32/src
)
# The MCP2515 driver header needs the CAN protocol header from shared/
# and the config.h from rt-esp32/src. No FreeRTOS or HAL stubs needed
# since we only import the CNF constants.

# ── RT CAN router test ──────────────────────────────────────────────
add_executable(rt_can_rx_router
    test/test_rt_can_rx_router.cpp
)
target_link_libraries(rt_can_rx_router PRIVATE freertos hal_stubs virtual_can)
target_include_directories(rt_can_rx_router PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_SOURCE_DIR}/../shared
    ${CMAKE_SOURCE_DIR}/../rt-esp32/src
)

# ── RT CAN dispatch test ─────────────────────────────────────────────
add_executable(rt_can_dispatch
    test/test_rt_can_dispatch.cpp
    ${CMAKE_SOURCE_DIR}/../rt-esp32/src/physics_model.cpp
)
target_link_libraries(rt_can_dispatch PRIVATE freertos hal_stubs virtual_can)
target_include_directories(rt_can_dispatch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_SOURCE_DIR}/../shared
    ${CMAKE_SOURCE_DIR}/../rt-esp32/src
)

# ── RT safety monitor test ──────────────────────────────────────────
add_executable(rt_safety_monitor
    test/test_rt_safety_monitor.cpp
    ${CMAKE_SOURCE_DIR}/../rt-esp32/src/physics_model.cpp
)
target_link_libraries(rt_safety_monitor PRIVATE freertos hal_stubs virtual_can)
target_include_directories(rt_safety_monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/shadow
    ${CMAKE_SOURCE_DIR}/../shared
    ${CMAKE_SOURCE_DIR}/../rt-esp32/src
)

# ── SYS CAN dispatch test ────────────────────────────────────────────
add_executable(sys_can_dispatch
    test/test_sys_can_dispatch.cpp
)
target_include_directories(sys_can_dispatch PRIVATE
    ${CMAKE_SOURCE_DIR}/../shared
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
target_include_directories(dual_heartbeat PRIVATE
    ${CMAKE_SOURCE_DIR}/../shared
    ${CMAKE_SOURCE_DIR}/../rt-esp32/src
)

# ── CTest ────────────────────────────────────────────────────────────
enable_testing()
add_test(NAME smoke               COMMAND smoke_test)
add_test(NAME protocol_roundtrip   COMMAND protocol_roundtrip)
add_test(NAME checksum_full       COMMAND checksum_full)
add_test(NAME mcp2515_config      COMMAND mcp2515_config)
add_test(NAME rt_can_rx_router    COMMAND rt_can_rx_router)
add_test(NAME rt_can_dispatch     COMMAND rt_can_dispatch)
add_test(NAME sys_can_dispatch    COMMAND sys_can_dispatch)
add_test(NAME task_watchdog       COMMAND task_watchdog)
add_test(NAME rt_safety_monitor   COMMAND rt_safety_monitor)
add_test(NAME heartbeat_recovery  COMMAND heartbeat_recovery)
add_test(NAME watchdog_wraparound COMMAND watchdog_wraparound)
add_test(NAME dual_heartbeat      COMMAND dual_heartbeat)

# ── Status ───────────────────────────────────────────────────────────
message(STATUS "Native test build configured.")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  FreeRTOS port: POSIX (${FREERTOS_PORT_DIR})")
