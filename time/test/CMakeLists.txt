cmake_minimum_required(VERSION 3.16)
project(timer_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(timer_test main.cpp ../Timer.cpp)

target_include_directories(timer_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../../thread_pool)
find_package(Threads REQUIRED)
target_link_libraries(timer_test PRIVATE Threads::Threads)