cmake_minimum_required(VERSION 3.20)
project(pipeline_builder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

FetchContent_MakeAvailable(googletest)

enable_testing()

add_library(pipeline_builder INTERFACE)
target_include_directories(pipeline_builder INTERFACE include)

# Tests
add_executable(pipeline_tests test/pipeline_tests.cpp)
target_link_libraries(pipeline_tests
  pipeline_builder
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(pipeline_tests)