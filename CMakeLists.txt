cmake_minimum_required(VERSION 3.16)
project(netio_io_tracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

include(CTest)

add_executable(task_test
    test/task_test.cpp
)
target_include_directories(task_test PRIVATE include)

add_executable(event_loop_test
    test/event_loop_test.cpp
    src/event_loop.cpp
)
target_include_directories(event_loop_test PRIVATE include)
target_link_libraries(event_loop_test PRIVATE Threads::Threads)

if(BUILD_TESTING)
    add_test(NAME task_test COMMAND task_test)
    add_test(NAME event_loop_test COMMAND event_loop_test)
endif()

