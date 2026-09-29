cmake_minimum_required(VERSION 3.8)
project(ros2_modbus_gateway)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find ROS 2 dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(unique_identifier_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(rmw_fastrtps_cpp REQUIRED)

# Boost (Asio and System)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

# Generate ROS 2 Messages and Services
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/PlcState.msg"
  "msg/SafetyAlarm.msg"
  "srv/TriggerCommand.srv"
  "srv/ClearFault.srv"
  DEPENDENCIES builtin_interfaces unique_identifier_msgs
)

rosidl_get_typesupport_target(cpp_typesupport_target "${PROJECT_NAME}" "rosidl_typesupport_cpp")

# Core library (domain, config, clock, safety, buffer, modbus_client, station_context, runtime)
add_library(${PROJECT_NAME}_core
  src/config.cpp
  src/monotonic_clock.cpp
  src/safety_monitor.cpp
  src/gateway_buffer.cpp
  src/modbus_client.cpp
  src/station_context.cpp
  src/gateway_runtime.cpp
)
target_include_directories(${PROJECT_NAME}_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}_core PUBLIC
  Boost::system
  Threads::Threads
)

# ROS 2 Node library
add_library(${PROJECT_NAME}_node
  src/gateway_node.cpp
)
target_include_directories(${PROJECT_NAME}_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(${PROJECT_NAME}_node PUBLIC
  ${PROJECT_NAME}_core
  rclcpp::rclcpp
  "${cpp_typesupport_target}"
  Boost::system
  Threads::Threads
)

# Main executable
add_executable(gateway_node
  src/main.cpp
)
target_link_libraries(gateway_node
  ${PROJECT_NAME}_node
  ${PROJECT_NAME}_core
  rclcpp::rclcpp
  "${cpp_typesupport_target}"
  Boost::system
  Threads::Threads
)

# Install rules
install(
  DIRECTORY include/
  DESTINATION include
)

install(
  TARGETS ${PROJECT_NAME}_core ${PROJECT_NAME}_node
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  TARGETS gateway_node
  DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config
)

install(
  DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

# Testing
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_config
    tests/test_config.cpp
  )
  target_link_libraries(test_config
    ${PROJECT_NAME}_core
  )

  ament_add_gtest(test_safety_monitor
    tests/test_safety_monitor.cpp
  )
  target_link_libraries(test_safety_monitor
    ${PROJECT_NAME}_core
  )

  ament_add_gtest(test_gateway_buffer
    tests/test_gateway_buffer.cpp
  )
  target_link_libraries(test_gateway_buffer
    ${PROJECT_NAME}_core
  )

  ament_add_gtest(test_modbus_client
    tests/test_modbus_client.cpp
  )
  target_link_libraries(test_modbus_client
    ${PROJECT_NAME}_core
  )

  ament_add_gtest(test_gateway_runtime
    tests/test_gateway_runtime.cpp
  )
  target_link_libraries(test_gateway_runtime
    ${PROJECT_NAME}_core
  )

  ament_add_gtest(test_gateway_node
    tests/test_gateway_node.cpp
  )
  target_link_libraries(test_gateway_node
    ${PROJECT_NAME}_node
    ${PROJECT_NAME}_core
    rclcpp::rclcpp
    "${cpp_typesupport_target}"
  )
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME}_core ${PROJECT_NAME}_node)
ament_export_dependencies(
  rclcpp
  builtin_interfaces
  unique_identifier_msgs
  rosidl_default_runtime
  rmw_fastrtps_cpp
  Boost
)

ament_package()
