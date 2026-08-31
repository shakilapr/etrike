# RT-AURIX-Lite host tests. Owned by this subtree; included by
# native-test via add_subdirectory. One CTest invocation overall.

enable_testing()

set(RTA_TESTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ── Kinematics differential test ─────────────────────────────────────
# Compiles the original rt-esp32 physics_model as a read-only oracle and
# asserts rta::Kinematics matches it exactly across an input grid.
add_executable(rta_kinematics_diff
    ${RTA_TESTS_ROOT}/test_kinematics_diff.cpp
    ${ETRIKE_ROOT}/rt-esp32/src/physics_model.cpp
)
target_link_libraries(rta_kinematics_diff PRIVATE rta_core)
target_include_directories(rta_kinematics_diff PRIVATE
    ${ETRIKE_ROOT}/rt-esp32/src        # physics_model.h + config.h
    ${ETRIKE_ROOT}/shared              # shared_config.h
    ${ETRIKE_ROOT}/native-test/hal/shadow  # esp_log.h stub
    ${ETRIKE_ROOT}                     # protocol/compat, protocol/core
)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(rta_kinematics_diff PRIVATE -static)
endif()
add_test(NAME rta_kinematics_diff COMMAND rta_kinematics_diff)

# ── Steering controller unit test ───────────────────────────────────
add_executable(rta_steering
    ${RTA_TESTS_ROOT}/test_steering.cpp
)
target_link_libraries(rta_steering PRIVATE rta_core)
add_test(NAME rta_steering COMMAND rta_steering)

# ── Brake controller unit test ──────────────────────────────────────
add_executable(rta_brake
    ${RTA_TESTS_ROOT}/test_brake.cpp
)
target_link_libraries(rta_brake PRIVATE rta_core)
add_test(NAME rta_brake COMMAND rta_brake)

# ── Mode/ESTOP state model unit test ───────────────────────────────
add_executable(rta_mode
    ${RTA_TESTS_ROOT}/test_mode.cpp
)
target_link_libraries(rta_mode PRIVATE rta_core)
add_test(NAME rta_mode COMMAND rta_mode)
