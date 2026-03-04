cmake_minimum_required(VERSION 3.20)
project(kalshi_backtesting_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(kalshi_backtest
  src/main.cpp
  src/cli.cpp
  src/csv_reader.cpp
  src/engine.cpp
  src/kalshi_fetch.cpp
)

target_include_directories(kalshi_backtest PRIVATE include)

if(MSVC)
  target_compile_options(kalshi_backtest PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(kalshi_backtest PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wdouble-promotion
  )
endif()

option(ENABLE_SANITIZERS "Enable Address/Undefined sanitizers in Debug for GCC/Clang" ON)
if(ENABLE_SANITIZERS AND NOT MSVC)
  target_compile_options(kalshi_backtest PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer>
  )
  target_link_options(kalshi_backtest PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
  )
endif()

enable_testing()
add_executable(kalshi_backtest_tests
  tests/test_main.cpp
  tests/test_rolling_stats.cpp
  tests/test_spike_detection.cpp
  tests/test_portfolio.cpp
  tests/test_max_drawdown.cpp
)
target_include_directories(kalshi_backtest_tests PRIVATE include)
if(MSVC)
  target_compile_options(kalshi_backtest_tests PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(kalshi_backtest_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wdouble-promotion
  )
endif()
add_test(NAME kalshi_backtest_tests COMMAND kalshi_backtest_tests)
