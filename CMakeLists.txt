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

add_executable(netio
    test/main.cpp
    src/event_loop.cpp
    src/tracer.cpp
)
target_include_directories(netio PRIVATE include)
target_link_libraries(netio PRIVATE Threads::Threads)

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

    add_test(NAME mode_sync
        COMMAND netio
            --mode sync
            --disk-us 1000
            --pool 2
            --win-sec 1
            --out ${CMAKE_CURRENT_BINARY_DIR}/sync-test.pcap
    )
    add_test(NAME mode_thread
        COMMAND netio
            --mode thread
            --disk-us 1000
            --pool 2
            --win-sec 1
            --out ${CMAKE_CURRENT_BINARY_DIR}/thread-test.pcap
    )
    add_test(NAME mode_coro
        COMMAND netio
            --mode coro
            --disk-us 1000
            --pool 2
            --win-sec 1
            --out ${CMAKE_CURRENT_BINARY_DIR}/coro-test.pcap
    )
    add_test(NAME mode_all
        COMMAND netio
            --mode all
            --disk-us 1000
            --pool 2
            --win-sec 1
            --out ${CMAKE_CURRENT_BINARY_DIR}/all-test.pcap
    )

    set_tests_properties(mode_sync mode_thread mode_coro mode_all PROPERTIES
        LABELS integration
        TIMEOUT 15
    )
endif()
