cmake_minimum_required(VERSION 3.16)
project(MemoryPoolBenchmark)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_executable(memory_pool_benchmark 
    main.cpp 
    ../memory_pool.cpp  
    ../chunk.cpp
)

target_include_directories(memory_pool_benchmark 
    PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/.. 
)

find_package(Threads REQUIRED)
target_link_libraries(memory_pool_benchmark PRIVATE Threads::Threads)