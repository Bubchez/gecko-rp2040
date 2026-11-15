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
  src/transport/simulated.cpp src/session/session.cpp)
target_include_directories(gecko_core PUBLIC include)
target_link_libraries(gecko_core PUBLIC gecko_build_options)

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
  find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
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
endif()
