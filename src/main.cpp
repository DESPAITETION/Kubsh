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
std::vector<std::string> g_command_history;
std::string g_history_file_path;

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

// ========== Управление историей ==========
void loadHistory() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    
    g_history_file_path = std::string(home) + "/.kubsh_history";
    
    std::ifstream file(g_history_file_path);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line) && g_command_history.size() < 1000) {
        if (!line.empty()) {
            g_command_history.push_back(line);
        }
    }
    
    file.close();
}

void saveHistory() {
    std::ofstream file(g_history_file_path);
    if (!file.is_open()) return;
    
    for (const auto& cmd : g_command_history) {
        file << cmd << std::endl;
    }
    
    file.close();
}

void addToHistory(const std::string& command) {
    if (command.empty() || command == "\\q") return;
    
    g_command_history.push_back(command);
    if (g_command_history.size() > 1000) {
        g_command_history.erase(g_command_history.begin());
    }
}

void printHistory() {
    for (size_t i = 0; i < g_command_history.size(); ++i) {
        std::cout << i + 1 << ": " << g_command_history[i] << std::endl;
    }
}

// ========== Вывод ошибки "command not found" ==========
void printCommandNotFound(const std::string& cmd) {
    std::cout << cmd << ": command not found" << std::endl;
}

// ========== Команда echo ==========
void executeEcho(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        std::cout << args[i];
        if (i != args.size() - 1) std::cout << " ";
    }
    std::cout << std::endl;
}

// ========== Команда env ==========
void executeEnv(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        // Вывод всех переменных окружения
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
                // Разделяем по : (для PATH)
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

// ========== Команда lsblk (информация о дисках) ==========
void executeLsblk(const std::vector<std::string>& args) {
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
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
        // Дочерний процесс
        std::vector<char*> argv;
        argv.push_back(strdup(command.c_str()));
        for (const auto& arg : args) {
            argv.push_back(strdup(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(command.c_str(), argv.data());
        
        // Если execvp вернулся - ошибка
        printCommandNotFound(command);
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

// ========== VFS (Virtual File System) ==========
void initVFS() {
    // Получаем домашнюю директорию
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    
    std::string vfsDir = std::string(home) + "/users";
    
    // Создаем директорию, если её нет
    mkdir(vfsDir.c_str(), 0755);
    
    // Читаем /etc/passwd и создаем VFS
    std::ifstream passwd("/etc/passwd");
    if (!passwd.is_open()) {
        return;
    }
    
    std::string line;
    while (std::getline(passwd, line)) {
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
            
            // Создаем директорию пользователя
            std::string userDir = vfsDir + "/" + username;
            mkdir(userDir.c_str(), 0755);
            
            // Создаем файл id
            std::ofstream idFile(userDir + "/id");
            if (idFile.is_open()) {
                idFile << uid;
                idFile.close();
            }
            
            // Создаем файл home
            std::ofstream homeFile(userDir + "/home");
            if (homeFile.is_open()) {
                homeFile << home_dir;
                homeFile.close();
            }
            
            // Создаем файл shell
            std::ofstream shellFile(userDir + "/shell");
            if (shellFile.is_open()) {
                shellFile << shell;
                shellFile.close();
            }
        }
    }
    
    passwd.close();
}

// ========== Главная функция ==========
int main() {
    // Устанавливаем unitbuf для корректной работы
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Загружаем историю
    loadHistory();
    
    // Инициализируем VFS
    initVFS();
    
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGHUP, &sa, NULL);
    
    // Проверяем, интерактивный ли режим
    bool interactive = isatty(STDIN_FILENO);
    
    std::string line;
    
    // Если интерактивный режим - показываем приветствие
    if (interactive) {
        std::cout << "Kubsh v1.0" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
        std::cout << "Enter a string: ";
    }
    
    while (true) {
        // Чтение строки
        if (!std::getline(std::cin, line)) {
            if (interactive) {
                std::cout << std::endl;
            }
            break;  // Ctrl+D
        }
        
        if (line.empty()) {
            continue;
        }
        
        // Парсинг команды
        std::vector<std::string> args = parseCommand(line);
        if (args.empty()) continue;
        
        std::string command = args[0];
        
        // Добавляем в историю (кроме специальных команд)
        if (command != "\\q" && command != "history") {
            addToHistory(line);
        }
        
        // Обработка команд
        if (command == "\\q") {
            break;
        } else if (command == "history") {
            printHistory();
        } else if (command == "echo") {
            executeEcho(args);
        } else if (command == "\\e") {
            executeEnv(args);
        } else if (command == "\\l") {
            executeLsblk(args);
        } else {
            // Внешняя команда
            executeExternal(command, std::vector<std::string>(args.begin() + 1, args.end()));
        }
    }
    
    // Сохраняем историю
    saveHistory();
    
    if (interactive) {
        std::cout << "Goodbye!" << std::endl;
    }
    
    return 0;
}