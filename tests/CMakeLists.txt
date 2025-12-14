cmake_minimum_required(VERSION 3.16)
project(NetworkLibTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 包含网络库头文件
include_directories(
    ${CMAKE_SOURCE_DIR}/../net
    ${CMAKE_SOURCE_DIR}/../logger
    ${CMAKE_SOURCE_DIR}/../memory
    ${CMAKE_SOURCE_DIR}/../thread_pool
    ${CMAKE_SOURCE_DIR}/../time
    ${CMAKE_SOURCE_DIR}/src

)

# 查找线程库
find_package(Threads REQUIRED)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -DDEBUG)
    message(STATUS "Building in Debug mode")
else()
    add_compile_options(-O2 -DNDEBUG)
    message(STATUS "Building in Release mode")
endif()

# 主服务器程序
add_executable(http_server
    src/httpServer.cpp
)

# 链接库
target_link_libraries(http_server PRIVATE 
    pthread
    ${CMAKE_THREAD_LIBS_INIT}
    ${CMAKE_SOURCE_DIR}/libAZH_lib.a
)


# 安装目标
install(TARGETS http_server 
    RUNTIME DESTINATION bin
)

# 安装脚本
install(DIRECTORY scripts/ DESTINATION scripts)
install(DIRECTORY config/ DESTINATION config)

# 添加测试
enable_testing()
add_test(NAME test_echo_server
    COMMAND ${CMAKE_BINARY_DIR}/bin/test_client
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)