#include "Shell.h"
#include <iostream>

Shell::Shell() : prompt("kubsh> "), running(true) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
}

void Shell::run() {
    // Эта функция теперь не используется в main.cpp
    // Но оставляем для совместимости
}