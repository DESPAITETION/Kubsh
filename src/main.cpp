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
#include <iomanip>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>  // ДОБАВЛЕНО
#include <grp.h>  // ДОБАВЛЕНО
#include "VFSManager.h"
#include "HistoryManager.h"

volatile sig_atomic_t g_reload_config = 0;

// Обработчик сигнала SIGHUP
void handleSIGHUP(int sig) {
    (void)sig;
    std::cout << "\nConfiguration reloaded" << std::endl;
    g_reload_config = 1;
}

// Функция для разбора команды на аргументы
std::vector<std::string> parseCommand(const std::string& input) {
    std::vector<std::string> args;
    std::stringstream ss(input);
    std::string token;
    
    while (ss >> token) {
        // Удаляем кавычки если они есть
        if (token.size() >= 2 && 
            ((token.front() == '\'' && token.back() == '\'') ||
             (token.front() == '"' && token.back() == '"'))) {
            token = token.substr(1, token.size() - 2);
        }
        args.push_back(token);
    }
    
    return args;
}

// Сообщение "команда не найдена"
void printCommandNotFound(const std::string& cmd) {
    std::cout << cmd << ": command not found" << std::endl;
}

// Команда echo (требование 5)
void executeEcho(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        std::cout << args[i];
        if (i != args.size() - 1) std::cout << " ";
    }
    std::cout << std::endl;
}

// Команда \e для переменных окружения (требование 7)
void printEnvironmentVariable(const std::string& varName) {
    const char* value = std::getenv(varName.c_str());
    if (value) {
        std::string envValue = value;
        if (varName == "PATH") {
            // Выводим PATH построчно с нумерацией
            std::stringstream ss(envValue);
            std::string path;
            int count = 1;
            while (std::getline(ss, path, ':')) {
                std::cout << std::setw(3) << count++ << ": " << path << std::endl;
            }
        } else {
            std::cout << varName << "=" << envValue << std::endl;
        }
    } else {
        std::cout << "Environment variable " << varName << " not found" << std::endl;
    }
}

// Команда \l для информации о диске (требование 10)
void executeDiskInfo(const std::string& disk) {
    std::string command = "lsblk " + disk;
    int result = system(command.c_str());
    
    if (result != 0) {
        command = "fdisk -l " + disk + " 2>/dev/null";
        result = system(command.c_str());
        
        if (result != 0) {
            std::cout << "Cannot get disk information for " << disk << std::endl;
        }
    }
}

// Монтирование VFS (требование 11)
void mountVFS() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        } else {
            home = "/tmp";
        }
    }
    
    std::string vfsDir = std::string(home) + "/users";
    
    // Создаем директорию если не существует
    mkdir(vfsDir.c_str(), 0755);
    
    std::cout << "VFS mounted at: " << vfsDir << std::endl;
}

// Выполнение внешних команд (требование 8)
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
        
        // Пытаемся выполнить команду
        execvp(command.c_str(), argv.data());
        
        // Если execvp не удался - команда не найдена
        printCommandNotFound(command);
        exit(1);
    } else if (pid > 0) {
        // Родительский процесс - ждем завершения
        int status;
        waitpid(pid, &status, 0);
    } else {
        std::cerr << "Failed to create process" << std::endl;
    }
}

// Функция для очистки экрана (дополнительная)
void clearScreen() {
    std::cout << "\033[2J\033[1;1H";
}

int main() {
    // Устанавливаем буферизацию для корректного вывода
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Создаем менеджеры
    VFSManager vfsManager;
    HistoryManager historyManager;
    
    // Монтируем VFS при запуске (требование 11)
    mountVFS();
    vfsManager.createVFS();
    
    // Настройка обработки сигнала SIGHUP (требование 9)
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    // Проверяем, интерактивный ли режим
    bool interactive = isatty(STDIN_FILENO);
    
    // Приветствие для интерактивного режима
    if (interactive) {
        std::cout << "========================================" << std::endl;
        std::cout << "           kubsh v1.0.0" << std::endl;
        std::cout << "     Custom Shell Implementation" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Commands:" << std::endl;
        std::cout << "  \\q              - Exit shell" << std::endl;
        std::cout << "  \\e <VAR>        - Show environment variable" << std::endl;
        std::cout << "  \\l <disk>       - Show disk info (e.g., \\l /dev/sda)" << std::endl;
        std::cout << "  echo <text>     - Print text" << std::endl;
        std::cout << "  history         - Show command history" << std::endl;
        std::cout << "  clear           - Clear screen" << std::endl;
        std::cout << "  <external_cmd>  - Run external command" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
    }
    
    // Проверка новых пользователей при запуске
    vfsManager.checkAndCreateNewUsers();
    
    // Главный цикл шелла (требования 1, 2)
    std::string line;
    while (true) {
        // Вывод промпта в интерактивном режиме
        if (interactive) {
            std::cout << "kubsh> ";
        }
        
        // Чтение ввода (требование 1)
        if (std::getline(std::cin, line)) {
            // Команда выхода (требование 3)
            if (line == "\\q") {
                break;
            }
            
            // Пропускаем пустые строки
            if (line.empty()) {
                continue;
            }
            
            // Добавляем команду в историю (требование 4)
            historyManager.addToHistory(line);
            
            // Разбираем команду на аргументы
            std::vector<std::string> args = parseCommand(line);
            if (args.empty()) {
                continue;
            }
            
            std::string command = args[0];
            
            // Обработка встроенных команд
            if (command == "echo") {
                executeEcho(args);
            }
            else if (command == "\\e") {
                if (args.size() > 1) {
                    printEnvironmentVariable(args[1]);
                } else {
                    std::cout << "Usage: \\e <VARIABLE_NAME>" << std::endl;
                    std::cout << "Example: \\e PATH" << std::endl;
                }
            }
            else if (command == "\\l") {
                if (args.size() > 1) {
                    executeDiskInfo(args[1]);
                } else {
                    std::cout << "Usage: \\l <DISK_DEVICE>" << std::endl;
                    std::cout << "Example: \\l /dev/sda" << std::endl;
                }
            }
            else if (command == "history") {
                historyManager.printHistory();
            }
            else if (command == "clear") {
                clearScreen();
            }
            else if (command == "debug") {
                // Специальная команда для отладки
                std::cout << "Debug info:" << std::endl;
                std::cout << "  Interactive mode: " << (interactive ? "yes" : "no") << std::endl;
                // Убраны system() вызовы чтобы избежать warnings
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd)) != nullptr) {
                    std::cout << "  Current dir: " << cwd << std::endl;
                }
                uid_t uid = getuid();
                struct passwd* pw = getpwuid(uid);
                if (pw) {
                    std::cout << "  User: " << pw->pw_name << std::endl;
                }
            }
            else {
                // Выполнение внешней команды (требование 8)
                executeExternal(command, std::vector<std::string>(args.begin() + 1, args.end()));
            }
        } 
        else {
            // EOF (Ctrl+D) - выход (требование 1, 2)
            if (interactive) {
                std::cout << std::endl;
            }
            break;
        }
        
        // Обработка сигнала SIGHUP (требование 9)
        if (g_reload_config) {
            g_reload_config = 0;
            vfsManager.checkAndCreateNewUsers();
            if (interactive) {
                std::cout << "VFS updated with current system users" << std::endl;
            }
        }
    }
    
    // Сохраняем историю при выходе (требование 4)
    historyManager.saveHistory();
    
    // Прощальное сообщение
    if (interactive) {
        std::cout << "Goodbye from kubsh!" << std::endl;
    }
    
    return 0;
}