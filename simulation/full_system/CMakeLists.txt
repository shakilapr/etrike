cmake_minimum_required(VERSION 3.16)
project(etrike_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Repo layout: simulation/full_system is two levels under the repo root.
set(ETRIKE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(etrike_sim
    scenarios/main.cpp
    ${ETRIKE_ROOT}/rt-esp32/src/core/rt_core.cpp
)

target_include_directories(etrike_sim PRIVATE
    ${ETRIKE_ROOT}                  # protocol/
    ${ETRIKE_ROOT}/rt-esp32/src     # core/rt_core.h + safety_stream_loss.h
    ${CMAKE_CURRENT_SOURCE_DIR}     # runtime/, models/, vehicle/, scenarios/
)

# Static link avoids runtime DLL dependencies (libgcc_s_seh-1.dll etc.).
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(etrike_sim PRIVATE -static)
endif()

enable_testing()
add_test(NAME etrike_sim COMMAND etrike_sim)
