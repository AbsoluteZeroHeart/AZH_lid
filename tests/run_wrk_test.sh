#!/bin/bash
# run_wrk_test.sh

PORT=${1:-8080}
THREADS=${2:-12}
CONNECTIONS=${3:-1000}
DURATION=${4:-30s}

echo "=== WRK Benchmark Test ==="
echo "Server: localhost:$PORT"
echo "Threads: $THREADS"
echo "Connections: $CONNECTIONS"
echo "Duration: $DURATION"
echo "=========================="

# 检查wrk是否安装
if ! command -v wrk &> /dev/null; then
    echo "Error: wrk is not installed!"
    echo "Installation:"
    echo "  Ubuntu: sudo apt-get install wrk"
    echo "  Mac: brew install wrk"
    echo "  Or build from source: https://github.com/wg/wrk"
    exit 1
fi

# 运行测试
echo -e "\n1. Testing with 100 connections..."
wrk -t4 -c100 -d10s http://localhost:$PORT/

echo -e "\n2. Testing with 1000 connections..."
wrk -t$THREADS -c1000 -d$DURATION http://localhost:$PORT/

echo -e "\n3. Testing with 5000 connections..."
wrk -t$THREADS -c5000 -d$DURATION http://localhost:$PORT/

echo -e "\n4. Testing with latency..."
wrk -t$THREADS -c1000 -d$DURATION --latency http://localhost:$PORT/

echo -e "\n5. Testing with script (mixed requests)..."
if [ -f "wrk_script.lua" ]; then
    wrk -t$THREADS -c1000 -d$DURATION -s wrk_script.lua http://localhost:$PORT/
else
    echo "Warning: wrk_script.lua not found, skipping script test"
fi

echo -e "\n=== Test Complete ==="