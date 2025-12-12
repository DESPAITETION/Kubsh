#!/bin/bash

echo "=== Command tests for kubsh ==="

make build

echo "1. Testing environment variables..."
export TEST_VAR="test_value"
echo "\\e TEST_VAR" | timeout 2 ./kubsh 2>&1 | grep -q "test_value" && echo "✓ Passed" || echo "✗ Failed"

echo "2. Testing command not found..."
echo "invalid_command_xyz" | timeout 2 ./kubsh 2>&1 | grep -i "not found\|command" && echo "✓ Passed" || echo "✗ Failed"

echo "3. Testing history file creation..."
echo "echo test_history" | timeout 2 ./kubsh 2>&1
if [ -f ~/.kubsh_history ]; then
    echo "✓ History file created"
else
    echo "✗ No history file"
fi

echo "=== Command tests completed ==="