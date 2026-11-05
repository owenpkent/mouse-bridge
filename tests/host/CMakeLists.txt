# Host-side tests: the firmware's pure logic, built for a PC, with no hardware.
#
#   cmake -S tests/host -B build-host
#   cmake --build build-host
#   ctest --test-dir build-host --output-on-failure
#
# Options (default OFF):
#   MOUSE_BRIDGE_HOST_WERROR    warnings as errors
#   MOUSE_BRIDGE_HOST_SANITIZE  AddressSanitizer + UBSan
#   MOUSE_BRIDGE_HOST_FUZZ      libFuzzer target report_fuzz, and a test that replays
#                               its corpus (Clang, needs MOUSE_BRIDGE_HOST_SANITIZE)

cmake_minimum_required(VERSION 3.22)
project(mouse_bridge_host_tests C)

option(MOUSE_BRIDGE_HOST_WERROR "Warnings as errors" OFF)
option(MOUSE_BRIDGE_HOST_SANITIZE "AddressSanitizer + UBSan" OFF)
option(MOUSE_BRIDGE_HOST_FUZZ "libFuzzer target and corpus replay" OFF)

if(MOUSE_BRIDGE_HOST_FUZZ AND NOT MOUSE_BRIDGE_HOST_SANITIZE)
  message(FATAL_ERROR "MOUSE_BRIDGE_HOST_FUZZ requires MOUSE_BRIDGE_HOST_SANITIZE")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SHARED_DIR ${REPO_ROOT}/firmware/mouse_bridge)
set(S31_DIR ${REPO_ROOT}/firmware/mouse_bridge_esp32s31/main)

add_compile_options(-Wall -Wextra)
if(MOUSE_BRIDGE_HOST_WERROR)
  add_compile_options(-Werror)
endif()
if(MOUSE_BRIDGE_HOST_SANITIZE)
  set(SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
  add_compile_options(${SANITIZE_FLAGS})
  add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

add_executable(test_bridge_logic test_bridge_logic.c)
target_include_directories(test_bridge_logic PRIVATE ${SHARED_DIR})
add_test(NAME bridge_logic COMMAND test_bridge_logic)

add_executable(test_mouse_report test_mouse_report.c ${S31_DIR}/mouse_report.c)
target_include_directories(test_mouse_report PRIVATE ${S31_DIR} ${SHARED_DIR})
add_test(NAME mouse_report COMMAND test_mouse_report)

if(MOUSE_BRIDGE_HOST_FUZZ)
  add_executable(report_fuzz ${REPO_ROOT}/tests/fuzz/report_fuzz.c ${S31_DIR}/mouse_report.c)
  target_include_directories(report_fuzz PRIVATE ${S31_DIR} ${SHARED_DIR})
  target_compile_options(report_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(report_fuzz PRIVATE -fsanitize=fuzzer)
  # -runs=0 only replays the corpus: every past crash stays a regression test.
  add_test(NAME report_fuzz_corpus COMMAND report_fuzz -runs=0 ${REPO_ROOT}/tests/fuzz/corpus)
endif()
