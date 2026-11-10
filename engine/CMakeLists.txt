cmake_minimum_required(VERSION 3.20)
project(Albatross VERSION 0.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ALBATROSS_BUILD_TESTS "Build unit tests" ON)
option(ALBATROSS_BUILD_TOOLS "Build the tuning tools and the tunable engine (testing aid, never shipped)" ON)
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

# Evaluation: depends on the core only. Search talks to it through eval/evaluation.h.
add_library(albatross_eval STATIC src/eval/classical.cpp src/eval/params.cpp)
target_link_libraries(albatross_eval PUBLIC albatross_core)

find_package(Threads REQUIRED)
add_library(albatross_search STATIC
  src/search/movepick.cpp
  src/search/search.cpp
  src/search/see.cpp
  src/search/timeman.cpp
  src/search/tt.cpp)
target_link_libraries(albatross_search PUBLIC albatross_core albatross_eval Threads::Threads)

add_library(albatross_uci STATIC src/uci/uci.cpp)
target_link_libraries(albatross_uci PUBLIC albatross_search)

add_executable(albatross src/main.cpp)
target_link_libraries(albatross PRIVATE albatross_uci)

if(ALBATROSS_BUILD_TESTS)
  enable_testing()
  add_executable(albatross_tests
    tests/test_main.cpp
    tests/test_attacks.cpp
    tests/test_eval.cpp
    tests/test_search.cpp
    tests/test_see_tt.cpp
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

# ---- Tuning tools: self-play data generator, Texel tuner and a tunable engine for ablation matches ----
# The tunable evaluation (-DALBATROSS_TUNABLE) has mutable weights; it is a separate set of libraries so the
# shipped engine keeps its constexpr weights.
if(ALBATROSS_BUILD_TOOLS)
  add_library(albatross_eval_tunable STATIC src/eval/classical.cpp src/eval/params.cpp)
  target_compile_definitions(albatross_eval_tunable PUBLIC ALBATROSS_TUNABLE)
  target_link_libraries(albatross_eval_tunable PUBLIC albatross_core)

  add_library(albatross_search_tunable STATIC
    src/search/movepick.cpp src/search/search.cpp src/search/see.cpp src/search/timeman.cpp src/search/tt.cpp)
  target_link_libraries(albatross_search_tunable PUBLIC albatross_core albatross_eval_tunable Threads::Threads)

  add_library(albatross_uci_tunable STATIC src/uci/uci.cpp)
  target_link_libraries(albatross_uci_tunable PUBLIC albatross_search_tunable)

  add_executable(albatross_tunable src/main.cpp)
  target_link_libraries(albatross_tunable PRIVATE albatross_uci_tunable)

  add_library(albatross_tools STATIC tools/datagen.cpp)
  target_include_directories(albatross_tools PUBLIC tools)
  target_link_libraries(albatross_tools PUBLIC albatross_search)

  add_library(albatross_tuning STATIC tools/tuning.cpp)
  target_include_directories(albatross_tuning PUBLIC tools)
  target_link_libraries(albatross_tuning PUBLIC albatross_eval_tunable Threads::Threads)

  add_executable(albatross_datagen tools/datagen_main.cpp)
  target_link_libraries(albatross_datagen PRIVATE albatross_tools)

  add_executable(albatross_tuner tools/tuner_main.cpp)
  target_link_libraries(albatross_tuner PRIVATE albatross_tuning)

  if(ALBATROSS_BUILD_TESTS)
    add_executable(albatross_tuning_tests tests/test_main.cpp tests/test_tuning.cpp)
    target_link_libraries(albatross_tuning_tests PRIVATE albatross_tuning albatross_uci_tunable)
    target_sources(albatross_tests PRIVATE tests/test_datagen.cpp)
    target_link_libraries(albatross_tests PRIVATE albatross_tools)
    add_test(NAME tuning COMMAND albatross_tuning_tests)
  endif()
endif()
