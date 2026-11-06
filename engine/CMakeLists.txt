cmake_minimum_required(VERSION 3.20)
project(Albatross VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ALBATROSS_BUILD_TESTS "Build unit tests" ON)
option(ALBATROSS_SLOW_TESTS "Register slow tests (deep perft) with CTest" OFF)

add_library(albatross_compile_flags INTERFACE)
if(MSVC)
  target_compile_options(albatross_compile_flags INTERFACE /W4 /permissive-)
else()
  target_compile_options(albatross_compile_flags INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Chess Core: no dependency on search / eval / uci.
add_library(albatross_core STATIC
  src/core/attacks.cpp
  src/core/bitboard.cpp
  src/core/init.cpp
  src/core/move.cpp
  src/core/movegen.cpp
  src/core/perft.cpp
  src/core/position.cpp
  src/core/zobrist.cpp)
target_include_directories(albatross_core PUBLIC src)
target_link_libraries(albatross_core PUBLIC albatross_compile_flags)

add_library(albatross_uci STATIC src/uci/uci.cpp)
target_link_libraries(albatross_uci PUBLIC albatross_core)

add_executable(albatross src/main.cpp)
target_link_libraries(albatross PRIVATE albatross_uci)

if(ALBATROSS_BUILD_TESTS)
  enable_testing()
  add_executable(albatross_tests
    tests/test_main.cpp
    tests/test_attacks.cpp
    tests/test_position.cpp
    tests/test_movegen.cpp
    tests/test_perft.cpp
    tests/test_uci.cpp)
  target_link_libraries(albatross_tests PRIVATE albatross_uci)
  add_test(NAME unit COMMAND albatross_tests)
  if(ALBATROSS_SLOW_TESTS)
    add_test(NAME perft_deep COMMAND albatross_tests --slow)
  endif()
endif()
