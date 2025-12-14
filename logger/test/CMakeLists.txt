cmake_minimum_required(VERSION 3.16)

project(logger_test)

set(CMAKE_CXX_STANDARD 17)

set(SOURCES ../logger.cpp ../pr.cpp ../log_queue.hpp )

add_executable(logger_test main.cpp ${SOURCES})

target_include_directories(logger_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Threads REQUIRED)
target_link_libraries(logger_test PRIVATE Threads::Threads)