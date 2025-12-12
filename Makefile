# Универсальный Makefile для Linux и Windows (внутри Docker)
.PHONY: all build run clean test package install

# Переменные
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g
TARGET := Kubsh
SRC_DIR := src
SOURCES := $(wildcard $(SRC_DIR)/*.cpp)

# Основные цели
all: build

build: $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

run: build
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o

# Тестирование
test: build
	@echo "Running basic tests..."
	@chmod +x tests/*.sh 2>/dev/null || true
	@./tests/test_basic.sh || true

# Сборка deb-пакета
package: build
	@echo "Building deb package..."
	@mkdir -p deb-packaging/usr/bin
	@cp $(TARGET) deb-packaging/usr/bin/
	@mkdir -p deb-packaging/DEBIAN
	@cp deb-packaging/control deb-packaging/DEBIAN/
	@dpkg-deb --build deb-packaging kubsh_1.0.0_amd64.deb

# Установка
install: package
	sudo dpkg -i kubsh_1.0.0_amd64.deb || true