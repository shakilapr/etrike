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

# ── CTest ────────────────────────────────────────────────────────────
enable_testing()
add_test(NAME smoke COMMAND smoke_test)

# ── Status ───────────────────────────────────────────────────────────
message(STATUS "Native test build configured.")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  FreeRTOS port: POSIX (${FREERTOS_PORT_DIR})")
