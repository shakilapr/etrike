# RT-AURIX-Lite firmware core — pure platform-agnostic logic.
# No FreeRTOS, no iLLD, no HAL dependency. Host-testable.
#
# Included by native-test via add_subdirectory(../rt-aurix-lite ...).
#
# Phase A is header-first: config/ + core/ types are pure headers. The
# rta_core STATIC library is added (with .cpp files) as domain/app modules
# land in later commits.

set(RTA_SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Interface target carrying the include paths needed by all rta code:
# repo root (protocol/core, protocol/codecs, shared), subset generated codec.
add_library(rta_headers INTERFACE)
target_include_directories(rta_headers INTERFACE
    ${ETRIKE_ROOT}
    ${ETRIKE_ROOT}/shared
    ${RTA_SRC_ROOT}
    ${RTA_SRC_ROOT}/protocol/generated/cpp
)

# rta_core — pure domain/app/protocol/ipc logic (grows with each commit).
add_library(rta_core STATIC
    ${RTA_SRC_ROOT}/domain/kinematics.cpp
    ${RTA_SRC_ROOT}/domain/steering.cpp
    ${RTA_SRC_ROOT}/domain/brake.cpp
    ${RTA_SRC_ROOT}/domain/mode.cpp
)
target_link_libraries(rta_core PUBLIC rta_headers)
