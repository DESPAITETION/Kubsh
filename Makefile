# Универсальный Makefile для Linux и Windows (внутри Docker)
.PHONY: all build run clean test package install docker-test

# Переменные
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g -pthread  # ДОБАВЬТЕ -pthread
TARGET := kubsh  # ИЗМЕНЕНО: было Kubsh, стало kubsh (строчными)
SRC_DIR := src
SOURCES := src/main.cpp  # Вместо $(wildcard $(SRC_DIR)/*.cpp)

# Основные цели
all: build

build: $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

run: build
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o kubsh_*.deb

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

# Тестирование в Docker (НОВАЯ ЦЕЛЬ)
docker-test: build
	@echo "Testing in Docker..."
	docker run --rm \
		-v $(PWD):/kubsh \
		-w /kubsh \
		tyvik/kubsh_test:master \
		/bin/bash -c "\
			cp kubsh /usr/local/bin/ && \
			chmod +x /usr/local/bin/kubsh && \
			pytest /opt/test_basic.py /opt/test_vfs.py -v"