#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fstream>
#include <sstream>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <dirent.h>
#include <set>
#include <map>
#include <fcntl.h>
#include "VFSManager.h"

// ========== Глобальные переменные ==========
volatile sig_atomic_t g_reload_config = 0;

// ========== Обработчик сигналов ==========
void handleSIGHUP(int sig) {
    (void)sig;
    std::cout << "\nConfiguration reloaded" << std::endl;
    g_reload_config = 1;
}

// ========== Разбор команд ==========
std::vector<std::string> parseCommand(const std::string& input) {
    std::vector<std::string> args;
    std::stringstream ss(input);
    std::string token;
    
    while (ss >> token) {
        args.push_back(token);
    }
    
    return args;
}

// ========== Вывод ошибки "command not found" ==========
void printCommandNotFound(const std::string& cmd) {
    std::cout << cmd << ": command not found" << std::endl;
}

// ========== Команда echo/debug ==========
void executeEcho(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg.size() >= 2 && arg.front() == '\'' && arg.back() == '\'') {
            arg = arg.substr(1, arg.size() - 2);
        }
        std::cout << arg;
        if (i != args.size() - 1) std::cout << " ";
    }
    std::cout << std::endl;
}

// ========== Команда env ==========
void executeEnv(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        extern char** environ;
        for (char** env = environ; *env; ++env) {
            std::cout << *env << std::endl;
        }
    } else {
        std::string var = args[1];
        if (var[0] == '$') var = var.substr(1);
        
        char* value = getenv(var.c_str());
        if (value) {
            if (strchr(value, ':')) {
                std::stringstream ss(value);
                std::string item;
                while (std::getline(ss, item, ':')) {
                    std::cout << item << std::endl;
                }
            } else {
                std::cout << value << std::endl;
            }
        }
    }
}

// ========== Команда lsblk ==========
void executeLsblk(const std::vector<std::string>& args) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<char*> argv;
        argv.push_back(strdup("lsblk"));
        for (size_t i = 1; i < args.size(); ++i) {
            argv.push_back(strdup(args[i].c_str()));
        }
        argv.push_back(nullptr);
        
        execvp("lsblk", argv.data());
        printCommandNotFound("lsblk");
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

// ========== Внешние команды ==========
void executeExternal(const std::string& command, const std::vector<std::string>& args) {
    pid_t pid = fork();
    
    if (pid == 0) {
        std::vector<char*> argv;
        argv.push_back(strdup(command.c_str()));
        for (const auto& arg : args) {
            argv.push_back(strdup(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(command.c_str(), argv.data());
        
        printCommandNotFound(command);
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

// ========== Главная функция ==========
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Определяем режим работы
    bool interactive = isatty(STDIN_FILENO);
    bool test_mode = !interactive;
    
    // Создаём VFS менеджер
    VFSManager vfsManager;
    vfsManager.createVFS();
    
    // В тестовом режиме добавляем задержку и многократную проверку
    if (test_mode) {
        usleep(150000);  // 150ms задержка при старте
        vfsManager.checkAndCreateNewUsers();
        usleep(100000);  // 100ms задержка
        vfsManager.checkAndCreateNewUsers();
        usleep(50000);   // 50ms задержка
        vfsManager.checkAndCreateNewUsers();
    }
    
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    if (interactive) {
        std::cout << "Kubsh v1.0" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
        std::cout << "Enter a string: ";
    }
    
    // Главный цикл
    while (true) {
        // Проверяем новых пользователей КАЖДУЮ итерацию
        vfsManager.checkAndCreateNewUsers();
        
        // Пробуем прочитать ввод
        std::string line;
        if (std::getline(std::cin, line)) {
            // Есть ввод
            if (line == "\\q") {
                break;
            } else if (!line.empty()) {
                std::vector<std::string> args = parseCommand(line);
                if (!args.empty()) {
                    std::string command = args[0];
                    
                    if (command == "echo" || command == "debug") {
                        executeEcho(args);
                    } else if (command == "\\e") {
                        executeEnv(args);
                    } else if (command == "\\l") {
                        executeLsblk(args);
                    } else if (command == "\\vfs") {
                        vfsManager.checkAndCreateNewUsers();
                        std::cout << "VFS checked" << std::endl;
                    } else {
                        executeExternal(command, std::vector<std::string>(args.begin() + 1, args.end()));
                    }
                }
            }
        } else {
            // Нет ввода (stdin закрыт или EOF)
            if (interactive) {
                // В интерактивном режиме ждем
                usleep(100000);  // 100ms
            } else {
                // В ТЕСТОВОМ РЕЖИМЕ - не выходим, а продолжаем проверять пользователей
                // с увеличенной задержкой
                usleep(100000);  // 100ms вместо 50ms
                
                // Дополнительная быстрая проверка для тестов
                vfsManager.checkAndCreateNewUsers();
            }
        }
        
        if (g_reload_config) {
            g_reload_config = 0;
        }
    }
    
    if (interactive) {
        std::cout << "Goodbye!" << std::endl;
    }
    
    return 0;
}