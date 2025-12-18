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

// ========== Создание пользователя ==========
bool createUser(const std::string& username) {
    struct passwd* pw = getpwnam(username.c_str());
    if (pw != nullptr) {
        return true;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        execlp("adduser", "adduser", "--disabled-password", "--gecos", "", 
               username.c_str(), NULL);
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    
    return false;
}

// ========== Создание VFS (ОЧЕНЬ ВАЖНО: вызывается ПЕРВОЙ) ==========
void createVFS() {
    std::string vfsDir = "/opt/users";
    
    // Создаём директорию
    mkdir(vfsDir.c_str(), 0755);
    
    // 1. Создаём VFS для существующих пользователей
    std::ifstream passwd("/etc/passwd");
    if (passwd.is_open()) {
        std::string line;
        while (std::getline(passwd, line)) {
            // Ищем пользователей с shell
            if (line.find("sh\n") == std::string::npos && 
                line.find("sh:") == std::string::npos) {
                continue;
            }
            
            std::stringstream ss(line);
            std::string token;
            std::vector<std::string> parts;
            
            while (std::getline(ss, token, ':')) {
                parts.push_back(token);
            }
            
            if (parts.size() >= 7) {
                std::string username = parts[0];
                std::string uid = parts[2];
                std::string home_dir = parts[5];
                std::string shell = parts[6];
                
                // Создаём директорию пользователя
                std::string userDir = vfsDir + "/" + username;
                mkdir(userDir.c_str(), 0755);
                
                // Создаём файлы
                std::ofstream idFile(userDir + "/id");
                if (idFile.is_open()) {
                    idFile << uid;
                    idFile.close();
                }
                
                std::ofstream homeFile(userDir + "/home");
                if (homeFile.is_open()) {
                    homeFile << home_dir;
                    homeFile.close();
                }
                
                std::ofstream shellFile(userDir + "/shell");
                if (shellFile.is_open()) {
                    shellFile << shell;
                    shellFile.close();
                }
            }
        }
        passwd.close();
    }
    
    // 2. Проверяем существующие директории и создаём пользователей
    DIR* dir = opendir(vfsDir.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_DIR) {
                std::string username = entry->d_name;
                if (username == "." || username == "..") continue;
                
                std::string userDir = vfsDir + "/" + username;
                std::string idFile = userDir + "/id";
                
                // Если нет файла id, создаём пользователя
                std::ifstream file(idFile);
                if (!file.is_open()) {
                    if (createUser(username)) {
                        // Обновляем файлы
                        struct passwd* pw = getpwnam(username.c_str());
                        if (pw) {
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
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
}

// ========== Главная функция ==========
int main() {
    // ========== ПЕРВОЕ И САМОЕ ВАЖНОЕ: создаём VFS ==========
    createVFS();
    
    // Настраиваем вывод
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    // Проверяем режим
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
    
    if (interactive) {
        std::cout << "Goodbye!" << std::endl;
    }
    
    return 0;
}