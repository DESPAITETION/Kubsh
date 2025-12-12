#include "Shell.h"
#include <iostream>

Shell::Shell() : prompt("kubsh> "), running(true) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
}

void Shell::run() {
    std::string line;
    
    std::cout << "Kubsh Shell started. Type '\\q' to exit." << std::endl;
    
    while (running) {
        std::cout << prompt;
        
        if (!std::getline(std::cin, line)) {
            std::cout << std::endl;
            break;  // Ctrl+D
        }
        
        if (line == "\\q") {
            running = false;
        } else {
            std::cout << "You entered: " << line << std::endl;
        }
    }
    
    std::cout << "Shell terminated." << std::endl;
}