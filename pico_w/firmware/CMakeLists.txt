# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.31.6)
set(PICO_BOARD pico_w CACHE STRING "Only supported board")
set(PICO_PLATFORM rp2040 CACHE STRING "Only supported MCU")
set(PICO_NO_PICOTOOL 1)
if(NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "Pinned external SDK")
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(gecko_pico_sim VERSION 0.2.1 LANGUAGES C CXX ASM)
if(NOT PICO_BOARD STREQUAL "pico_w" OR NOT PICO_PLATFORM STREQUAL "rp2040")
  message(FATAL_ERROR "Simulation image is restricted to RP2040 Pico W")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
pico_sdk_init()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../tools/firmware_identity.py
  OUTPUT_VARIABLE GECKO_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
string(LENGTH "${GECKO_BUILD_ID}" build_id_length)
if(NOT build_id_length EQUAL 64 OR NOT GECKO_BUILD_ID MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Invalid firmware input identity")
endif()
file(GLOB_RECURSE identity_inputs CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*" "${CMAKE_CURRENT_LIST_DIR}/../include/*"
  "${CMAKE_CURRENT_LIST_DIR}/../src/core/*" "${CMAKE_CURRENT_LIST_DIR}/../src/session/*"
  "${CMAKE_CURRENT_LIST_DIR}/../src/transport/*" "${CMAKE_CURRENT_LIST_DIR}/../src/simulation/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${identity_inputs}
  ${CMAKE_CURRENT_LIST_DIR}/../tools/firmware_identity.py)
add_executable(gecko_pico_sim
  main.cpp network.cpp
  ../src/core/protocol.cpp ../src/core/assembly.cpp
  ../src/transport/simulated.cpp ../src/session/session.cpp
  ../src/simulation/model.cpp ../src/simulation/config.cpp
  ../src/simulation/json.cpp ../src/simulation/api.cpp ../src/simulation/maintenance.cpp ../src/simulation/usb.cpp)
target_include_directories(gecko_pico_sim PRIVATE . ../include)
# No hardware_i2c, physical adapter, command dispatcher, RTOS or core1.
target_link_libraries(gecko_pico_sim PRIVATE pico_stdlib pico_cyw43_arch_lwip_poll
  pico_stdio_usb pico_rand pico_flash hardware_flash)
target_compile_definitions(gecko_pico_sim PRIVATE
  GECKO_FIRMWARE_BUILD_ID="${GECKO_BUILD_ID}"
  PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0 PICO_STDIO_USB_STDOUT_TIMEOUT_US=0
  CFG_TUD_CDC_TX_BUFSIZE=1024
  PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=0
  PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
  PICO_HEAP_SIZE=8192 PICO_STACK_SIZE=4096
  PICO_FLASH_ASSUME_CORE1_SAFE=1 GECKO_SIMULATION_ONLY=1)
# Apply strict warnings to project sources, avoiding SDK third-party warning policy.
get_target_property(project_sources gecko_pico_sim SOURCES)
set_source_files_properties(${project_sources} PROPERTIES COMPILE_OPTIONS
  "-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-Wshadow;-Werror;-fno-exceptions;-fno-rtti;-fstack-usage;-ffile-prefix-map=${CMAKE_CURRENT_LIST_DIR}/../..=/src;-ffile-prefix-map=${CMAKE_BINARY_DIR}=/build")
target_link_options(gecko_pico_sim PRIVATE -Wl,--print-memory-usage
  -L${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2040
  -Wl,-Map=${CMAKE_BINARY_DIR}/gecko_pico_sim.map)
get_filename_component(GECKO_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
target_compile_options(gecko_pico_sim PRIVATE
  -ffile-prefix-map=${GECKO_SOURCE_ROOT}=/src
  -ffile-prefix-map=${CMAKE_BINARY_DIR}=/build)
pico_set_linker_script(gecko_pico_sim ${CMAKE_CURRENT_LIST_DIR}/simulation.ld)
pico_enable_stdio_uart(gecko_pico_sim 0)
pico_enable_stdio_usb(gecko_pico_sim 1)
pico_set_printf_implementation(gecko_pico_sim compiler)
pico_add_bin_output(gecko_pico_sim)
