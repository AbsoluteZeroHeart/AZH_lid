#!/bin/bash

# 清理并重新构建
rm -rf build
mkdir build
cd build

# 配置和编译
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo ""
echo "测试"
echo "================="
./thread_test 

