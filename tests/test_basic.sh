#!/bin/bash

echo "=== Basic kubsh tests ==="

# Компилируем
echo "1. Testing compilation..."
make build
if [ $? -eq 0 ]; then
    echo "✓ Compilation successful"
else
    echo "✗ Compilation failed"
    exit 1
fi

# Тестируем базовый функционал
echo -e "\n2. Testing basic execution..."
echo "\\q" | timeout 2 ./kubsh > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ Basic execution works"
else
    echo "✗ Basic execution failed"
fi

# Тестируем echo
echo -e "\n3. Testing echo..."
result=$(echo "echo test123" | timeout 2 ./kubsh 2>&1 | grep -i "test123")
if [ ! -z "$result" ]; then
    echo "✓ Echo works"
else
    echo "✗ Echo failed"
fi

echo -e "\n=== Basic tests completed ==="