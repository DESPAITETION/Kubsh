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
    // Выводим в stdout для тестов
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

// ========== Команда echo/debug ==========
void executeEcho(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        // Убираем кавычки, если они есть
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

// ========== Функция для создания пользователя ==========
void checkAndCreateUser(const std::string& username) {
    // Проверяем, существует ли пользователь
    struct passwd* pw = getpwnam(username.c_str());
    if (pw != nullptr) {
        return; // Пользователь уже существует
    }
    
    // Создаем пользователя через adduser
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        execlp("adduser", "adduser", "--disabled-password", "--gecos", "", username.c_str(), NULL);
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, nullptr, 0);
    }
}

// ========== Проверка новых пользователей в VFS ==========
void checkNewVFSUsers() {
    std::string vfsDir = "/opt/users";
    DIR* dir = opendir(vfsDir.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            std::string username = entry->d_name;
            if (username == "." || username == "..") continue;
            
            std::string userDir = vfsDir + "/" + username;
            std::string idFile = userDir + "/id";
            
            // Если нет файла id, значит это новая директория
            std::ifstream file(idFile);
            if (!file.is_open()) {
                // Создаем пользователя
                checkAndCreateUser(username);
                
                // Создаем файлы в VFS
                struct passwd* pw = getpwnam(username.c_str());
                if (pw != nullptr) {
                    // Создаем файл id
                    std::ofstream idOut(idFile);
                    if (idOut.is_open()) {
                        idOut << pw->pw_uid;
                        idOut.close();
                    }
                    
                    // Создаем файл home
                    std::ofstream homeOut(userDir + "/home");
                    if (homeOut.is_open()) {
                        homeOut << pw->pw_dir;
                        homeOut.close();
                    }
                    
                    // Создаем файл shell
                    std::ofstream shellOut(userDir + "/shell");
                    if (shellOut.is_open()) {
                        shellOut << pw->pw_shell;
                        shellOut.close();
                    }
                }
            }
        }
    }
    
    closedir(dir);
}

// ========== VFS (Virtual File System) ==========
void initVFS() {
    // Для тестов всегда используем /opt/users
    std::string vfsDir = "/opt/users";
    
    // Создаем директорию, если её нет
    mkdir(vfsDir.c_str(), 0755);
    
    // Проверяем и создаем существующих пользователей
    checkNewVFSUsers();
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
    sa.sa_flags = SA_RESTART;
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
        // Проверяем новые пользователи в VFS
        checkNewVFSUsers();
        
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
        } else if (command == "echo" || command == "debug") {
            executeEcho(args);
        } else if (command == "\\e") {
            executeEnv(args);
        } else if (command == "\\l") {
            executeLsblk(args);
        } else {
            // Внешняя команда
            executeExternal(command, std::vector<std::string>(args.begin() + 1, args.end()));
        }
        
        // После обработки команды сбрасываем флаг сигнала
        if (g_reload_config) {
            g_reload_config = 0;
        }
    }
    
    // Сохраняем историю
    saveHistory();
    
    if (interactive) {
        std::cout << "Goodbye!" << std::endl;
    }
    
    return 0;
}