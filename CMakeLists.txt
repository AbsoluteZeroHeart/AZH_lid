cmake_minimum_required(VERSION 3.16)
project(AZH_lib)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES 
    ${CMAKE_CURRENT_SOURCE_DIR}/logger/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_pool/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/time/*.cpp
    )

add_library(AZH_lib ${SOURCES})

target_include_directories(AZH_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/logger
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/net
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_pool
    ${CMAKE_CURRENT_SOURCE_DIR}/time
)


