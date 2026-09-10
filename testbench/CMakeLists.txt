cmake_minimum_required(VERSION 3.20)
project(etrike-system-test VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ETRIKE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(etrike-system-test
    src/virtual_can_bus.cpp
    src/nodes/seb_model.cpp
    src/nodes/ses_model.cpp
    src/nodes/host_model.cpp
    src/nodes/mtr_node.cpp
    src/nodes/sys_node.cpp
    src/nodes/rt_node.cpp
    src/nodes/rm_operator_model.cpp
    src/test_bench.cpp
    scenarios/test_estop_recovery.cpp
    scenarios/scenarios_1_to_10.cpp
    scenarios/test_all_estop_triggers.cpp
    scenarios/test_estop_clear_and_activation.cpp
    scenarios/test_rm_operator.cpp
    scenarios/test_rm_signal_flow.cpp
    scenarios/main.cpp
    ${ETRIKE_ROOT}/sys-esp32/src/mode_manager.cpp
    ${ETRIKE_ROOT}/sys-esp32/src/safety_monitor.cpp
    ${ETRIKE_ROOT}/sys-esp32/src/inhibit_state.cpp
)

target_include_directories(etrike-system-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ETRIKE_ROOT}/native-test/hal/shadow
    ${ETRIKE_ROOT}
    ${ETRIKE_ROOT}/shared
    ${ETRIKE_ROOT}/sys-esp32/src
    ${ETRIKE_ROOT}/rt-esp32/src
    ${ETRIKE_ROOT}/mtr-stm32/src
    ${ETRIKE_ROOT}/rm-esp32-t12d/src
)

target_compile_definitions(etrike-system-test PRIVATE
    TESTING
    CONFIG_BENCH_SOLO
)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(etrike-system-test PRIVATE -static)
endif()
