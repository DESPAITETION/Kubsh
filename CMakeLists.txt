cmake_minimum_required(VERSION 3.10)
project(kubsh VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Исходные файлы
file(GLOB SOURCES "src/*.cpp")

# Исполняемый файл
add_executable(kubsh ${SOURCES})

# Установка
install(TARGETS kubsh DESTINATION bin)