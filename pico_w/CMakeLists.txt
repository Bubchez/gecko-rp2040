# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)
project(gecko_portable_core VERSION 0.1.0 LANGUAGES CXX)

option(GECKO_SANITIZERS "Enable address and undefined-behavior checks on the host" OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gecko_build_options INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(gecko_build_options INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
  if(GECKO_SANITIZERS)
    target_compile_options(gecko_build_options INTERFACE
      -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(gecko_build_options INTERFACE -fsanitize=address,undefined)
    # Avoid host ASan shadow-address collisions in Linux PIE executables.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_compile_options(gecko_build_options INTERFACE -fno-pie)
      target_link_options(gecko_build_options INTERFACE -no-pie)
    endif()
  endif()
elseif(GECKO_SANITIZERS)
  message(FATAL_ERROR "GECKO_SANITIZERS requires a GCC/Clang host compiler")
endif()

# Host-only, independently authored code. No SDK, GPIO, I2C or network imports.
add_library(gecko_core STATIC src/core/protocol.cpp src/core/assembly.cpp
  src/transport/simulated.cpp src/session/session.cpp src/simulation/model.cpp)
target_include_directories(gecko_core PUBLIC include)
target_link_libraries(gecko_core PUBLIC gecko_build_options)

add_library(gecko_runtime STATIC src/simulation/config.cpp src/simulation/json.cpp
  src/simulation/api.cpp src/simulation/maintenance.cpp)
target_link_libraries(gecko_runtime PUBLIC gecko_core)

# Native host process with bounded stdin/stdout IPC; never imports a physical HAL.
add_executable(gecko_mock_core src/host/mock_core.cpp)
target_link_libraries(gecko_mock_core PRIVATE gecko_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(gecko_core_tests tests/core_tests.cpp)
  target_link_libraries(gecko_core_tests PRIVATE gecko_core)
  foreach(case IN ITEMS
      bytes hex proxy temperature switches program fixed_frames frame_rejection
      filename clock reminders status assembly boundaries mutations)
    add_test(NAME core.${case} COMMAND gecko_core_tests ${case})
    set_tests_properties(core.${case} PROPERTIES TIMEOUT 30)
  endforeach()
  find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
  add_test(NAME core.fixtures COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_fixtures.py
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/protocol-examples.json $<TARGET_FILE:gecko_core_tests>)
  set_tests_properties(core.fixtures PROPERTIES TIMEOUT 30)
  add_executable(gecko_scenario_tests tests/scenario_tests.cpp)
  target_link_libraries(gecko_scenario_tests PRIVATE gecko_core)
  foreach(case IN ITEMS transport_success transport_faults transport_deadlines
      transport_capacity read_only handshake handshake_order duplicates freshness
      reconnect session_faults loss_recovery determinism temporal_boundaries)
    add_test(NAME scenario.${case} COMMAND gecko_scenario_tests ${case})
    set_tests_properties(scenario.${case} PROPERTIES TIMEOUT 30)
  endforeach()
  foreach(case IN ITEMS schema backend contract framing startup)
    add_test(NAME http.${case} COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/http_tests.py ${case} $<TARGET_FILE:gecko_mock_core>)
    set_tests_properties(http.${case} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(gecko_runtime_probe tests/runtime_probe.cpp)
  target_link_libraries(gecko_runtime_probe PRIVATE gecko_runtime)
  add_executable(gecko_runtime_tests tests/runtime_tests.cpp)
  target_link_libraries(gecko_runtime_tests PRIVATE gecko_runtime)
  foreach(case IN ITEMS config wifi json ownership)
    add_test(NAME runtime.${case} COMMAND gecko_runtime_tests ${case})
    set_tests_properties(runtime.${case} PROPERTIES TIMEOUT 30)
  endforeach()
  add_test(NAME runtime.contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_contract.py $<TARGET_FILE:gecko_runtime_probe> $<TARGET_FILE:gecko_mock_core>)
  set_tests_properties(runtime.contract PROPERTIES TIMEOUT 60)
  add_executable(gecko_network_tests tests/network_tests.cpp firmware/network.cpp)
  target_include_directories(gecko_network_tests PRIVATE tests/fake_pico firmware)
  target_link_libraries(gecko_network_tests PRIVATE gecko_runtime)
  add_test(NAME runtime.network COMMAND gecko_network_tests)
  set_tests_properties(runtime.network PROPERTIES TIMEOUT 30)
  add_test(NAME runtime.tools COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/firmware_tools_tests.py)
  set_tests_properties(runtime.tools PROPERTIES TIMEOUT 30)
endif()
