cmake_minimum_required(VERSION 3.8)
project(autoware_vehicle_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(autoware_control_msgs REQUIRED)
find_package(autoware_vehicle_msgs REQUIRED)
find_package(tier4_vehicle_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(lifecycle_msgs REQUIRED)
find_package(etrike_protocol REQUIRED)

include_directories(include)

add_executable(vehicle_bridge_node
  src/vehicle_bridge_node.cpp
)

ament_target_dependencies(vehicle_bridge_node
  rclcpp
  rclcpp_lifecycle
  autoware_control_msgs
  autoware_vehicle_msgs
  tier4_vehicle_msgs
  diagnostic_msgs
  lifecycle_msgs
  etrike_protocol
)

install(TARGETS
  vehicle_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  config/
  DESTINATION share/${PROJECT_NAME}/config
)

install(DIRECTORY
  launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

install(DIRECTORY
  include/
  DESTINATION include
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  add_executable(test_motion_conversion test/test_motion_conversion.cpp)
  target_include_directories(test_motion_conversion PRIVATE
    include
  )
  ament_target_dependencies(test_motion_conversion etrike_protocol)
  add_test(NAME motion_conversion COMMAND test_motion_conversion)
endif()

ament_package()
