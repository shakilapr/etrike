cmake_minimum_required(VERSION 3.8)
project(etrike_protocol LANGUAGES CXX)

find_package(ament_cmake REQUIRED)

add_library(etrike_protocol INTERFACE)
target_compile_features(etrike_protocol INTERFACE cxx_std_17)
target_include_directories(etrike_protocol INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
  "$<INSTALL_INTERFACE:include>"
)

install(TARGETS etrike_protocol EXPORT export_etrike_protocol)
install(DIRECTORY generated/cpp/
  DESTINATION include/protocol/generated/cpp
)

ament_export_targets(export_etrike_protocol HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_package()
