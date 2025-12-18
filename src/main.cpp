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
#include <sys/stat.h>
#include <dirent.h>
#include <thread>
#include <chrono>

// ========== Глобальные переменные ==========
volatile sig_atomic_t g_reload_config = 0;
volatile bool g_running = true;

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

// ========== Создание VFS ==========
void createVFS() {
    std::string vfsDir = "/opt/users";
    
    mkdir(vfsDir.c_str(), 0755);
    
    std::string rootDir = vfsDir + "/root";
    mkdir(rootDir.c_str(), 0755);
    
    std::ofstream idFile(rootDir + "/id");
    if (idFile.is_open()) {
        idFile << "0";
        idFile.close();
    }
    
    std::ofstream homeFile(rootDir + "/home");
    if (homeFile.is_open()) {
        homeFile << "/root";
        homeFile.close();
    }
    
    std::ofstream shellFile(rootDir + "/shell");
    if (shellFile.is_open()) {
        shellFile << "/bin/bash";
        shellFile.close();
    }
    
    std::cerr << "VFS created at " << vfsDir << std::endl;
}

// ========== Проверка и создание пользователей ==========
void checkAndCreateUsers() {
    std::string vfsDir = "/opt/users";
    DIR* dir = opendir(vfsDir.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            std::string username = entry->d_name;
            if (username == "." || username == ".." || username == "root") continue;
            
            std::string userDir = vfsDir + "/" + username;
            std::string idFile = userDir + "/id";
            
            // Если нет файла id, создаём пользователя
            std::ifstream file(idFile);
            if (!file.is_open()) {
                // Проверяем, существует ли пользователь
                struct passwd* pw = getpwnam(username.c_str());
                if (!pw) {
                    // Создаём пользователя через adduser
                    pid_t pid = fork();
                    if (pid == 0) {
                        execlp("adduser", "adduser", "--disabled-password", "--gecos", "",
                               username.c_str(), NULL);
                        exit(1);
                    } else if (pid > 0) {
                        int status;
                        waitpid(pid, &status, 0);
                        
                        // Ждём немного
                        usleep(50000);
                        
                        // Проверяем снова
                        pw = getpwnam(username.c_str());
                    }
                }
                
                if (pw) {
                    // Создаём файлы VFS
                    std::ofstream idOut(idFile);
                    if (idOut.is_open()) {
                        idOut << pw->pw_uid;
                        idOut.close();
                    }
                    
                    std::ofstream homeOut(userDir + "/home");
                    if (homeOut.is_open()) {
                        homeOut << pw->pw_dir;
                        homeOut.close();
                    }
                    
                    std::ofstream shellOut(userDir + "/shell");
                    if (shellOut.is_open()) {
                        shellOut << pw->pw_shell;
                        shellOut.close();
                    }
                    
                    std::cerr << "Created VFS entry for user: " << username << std::endl;
                }
            }
        }
    }
    
    closedir(dir);
}

// ========== Фоновый мониторинг ==========
void backgroundMonitor() {
    while (g_running) {
        checkAndCreateUsers();
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Проверяем каждые 100ms
    }
}

// ========== Главная функция ==========
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Создаём VFS
    createVFS();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    // Запускаем фоновый мониторинг
    std::thread monitor(backgroundMonitor);
    monitor.detach();
    
    // Основной цикл
    bool interactive = isatty(STDIN_FILENO);
    
    std::string line;
    
    if (interactive) {
        std::cout << "Kubsh v1.0" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
        std::cout << "Enter a string: ";
    }
    
    while (true) {
        if (!std::getline(std::cin, line)) {
            if (interactive) {
                std::cout << std::endl;
            }
            break;
        }
        
        if (line.empty()) {
            continue;
        }
        
        std::vector<std::string> args = parseCommand(line);
        if (args.empty()) continue;
        
        std::string command = args[0];
        
        if (command == "\\q") {
            break;
        } else if (command == "echo" || command == "debug") {
            executeEcho(args);
        } else if (command == "\\e") {
            executeEnv(args);
        } else if (command == "\\l") {
            executeLsblk(args);
        } else {
            executeExternal(command, std::vector<std::string>(args.begin() + 1, args.end()));
        }
        
        if (g_reload_config) {
            g_reload_config = 0;
        }
    }
    
    g_running = false;
    
    if (interactive) {
        std::cout << "Goodbye!" << std::endl;
    }
    
    return 0;
}