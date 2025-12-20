.PHONY: all build run clean test docker-test package check help

# Переменные
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g
TARGET := kubsh
SRC_DIR := src
SOURCES := $(wildcard $(SRC_DIR)/*.cpp)

# Основные цели
all: build

# Нативная сборка с g++
build: $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

run: build
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o kubsh_*.deb
	rm -rf deb-pkg-temp/

# Тестирование
test: build
	@echo "Running basic tests..."
	@chmod +x tests/*.sh 2>/dev/null || true
	@./tests/test_basic.sh
	@./tests/test_commands.sh
	@./tests/test_requirements.sh

# Сборка deb-пакета
package: build
	@echo "Building deb package..."
	@rm -rf deb-pkg-temp
	@mkdir -p deb-pkg-temp/usr/bin
	@mkdir -p deb-pkg-temp/DEBIAN
	@cp $(TARGET) deb-pkg-temp/usr/bin/
	@echo "Package: kubsh" > deb-pkg-temp/DEBIAN/control
	@echo "Version: 1.0.0" >> deb-pkg-temp/DEBIAN/control
	@echo "Section: utils" >> deb-pkg-temp/DEBIAN/control
	@echo "Priority: optional" >> deb-pkg-temp/DEBIAN/control
	@echo "Architecture: amd64" >> deb-pkg-temp/DEBIAN/control
	@echo "Depends: libc6 (>= 2.34), libstdc++6 (>= 11)" >> deb-pkg-temp/DEBIAN/control
	@echo "Maintainer: Student <student@university.edu>" >> deb-pkg-temp/DEBIAN/control
	@echo "Description: Custom shell with VFS support" >> deb-pkg-temp/DEBIAN/control
	@echo " A custom shell implementation with virtual filesystem" >> deb-pkg-temp/DEBIAN/control
	@echo " for user information management." >> deb-pkg-temp/DEBIAN/control
	@dpkg-deb --build deb-pkg-temp kubsh_1.0.0_amd64.deb
	@echo "✓ Package built: kubsh_1.0.0_amd64.deb"

# Docker тестирование
docker-test: build
	@echo "=== Docker Testing Commands ==="
	@echo "On Ubuntu VM, run:"
	@echo "  ./test_in_docker.sh"
	@echo ""
	@echo "Or manually:"
	@echo "  docker run --rm -v \$$(pwd):/kubsh -w /kubsh tyvik/kubsh_test:master make test"
	@echo "  docker run --rm -v \$$(pwd):/kubsh -w /kubsh tyvik/kubsh_test:master make package"

# Быстрая проверка
check: build
	@echo "=== Quick Test ==="
	@echo "Testing basic functionality..."
	@echo "\q" | timeout 2 ./$(TARGET) >/dev/null 2>&1 && echo "✓ Exit command works" || echo "✗ Exit command failed"
	@echo "echo test" | timeout 2 ./$(TARGET) 2>&1 | grep -q "test" && echo "✓ Echo command works" || echo "✗ Echo command failed"
	@echo "nonexistentcmd" | timeout 2 ./$(TARGET) 2>&1 | grep -i "not found" && echo "✓ Command validation works" || echo "✗ Command validation failed"

# CMake сборка
cmake-build:
	@mkdir -p build
	cd build && cmake .. && make

# Помощь
help:
	@echo "Available commands:"
	@echo "  make build      - Compile kubsh"
	@echo "  make run        - Run kubsh"
	@echo "  make test       - Run all tests"
	@echo "  make package    - Build deb package"
	@echo "  make check      - Quick functionality check"
	@echo "  make clean      - Clean build files"
	@echo "  make docker-test - Show Docker commands"
	@echo "  make cmake-build - Build with CMake"