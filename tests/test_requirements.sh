#!/bin/bash

echo "=== Testing all kubsh requirements ==="

make build

echo "Test 1: Basic input/output..."
echo "Hello kubsh" | timeout 2 ./kubsh 2>&1 | grep -q "Hello kubsh" && echo "✓ Passed" || echo "✗ Failed"

echo "Test 2: Exit command (\\q)..."
echo "\\q" | timeout 2 ./kubsh 2>&1 && echo "✓ Passed" || echo "✗ Failed"

echo "Test 3: Ctrl+D exit..."
timeout 2 ./kubsh <<< "echo test" && echo "✓ Passed" || echo "✗ Failed"

echo "Test 4: Command echo..."
echo "echo 123" | timeout 2 ./kubsh 2>&1 | grep -q "123" && echo "✓ Passed" || echo "✗ Failed"

echo "=== Requirement tests completed ==="