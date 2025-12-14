cmake_minimum_required(VERSION 3.16)
project(thread_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(thread_test main.cpp)
target_include_directories(thread_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Threads REQUIRED)
target_link_libraries(thread_test PRIVATE Threads::Threads)