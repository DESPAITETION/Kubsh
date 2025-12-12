#include <iostream>
#include <string>

int main() {
    // Важная строка для корректной работы потоков
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    std::cout << "Kubsh v1.0" << std::endl;
    std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
    
    std::string line;
    
    // Часть 1: Печатает введённую строку и выходит
    std::cout << "Enter a string: ";
    if (std::getline(std::cin, line)) {
        std::cout << "You entered: " << line << std::endl;
    }
    
    std::cout << "\nPart 2: Interactive mode" << std::endl;
    
    // Часть 2: Цикл с выходом по Ctrl+D
    while (true) {
        std::cout << "kubsh> ";
        
        if (!std::getline(std::cin, line)) {
            std::cout << std::endl;
            break;  // Ctrl+D
        }
        
        // Часть 3: Команда для выхода
        if (line == "\\q") {
            break;
        }
        
        // Печатаем введенную строку
        std::cout << line << std::endl;
    }
    
    std::cout << "Goodbye!" << std::endl;
    return 0;
}