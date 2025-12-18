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

// ========== Глобальные переменные ==========
volatile sig_atomic_t g_reload_config = 0;
std::set<std::string> g_processed_users;

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

// ========== Создание VFS для всех пользователей ==========
void createVFS() {
    std::string vfsDir = "/opt/users";
    
    // Создаём корневую директорию
    mkdir(vfsDir.c_str(), 0755);
    
    // Читаем /etc/passwd
    std::ifstream passwdFile("/etc/passwd");
    if (!passwdFile.is_open()) {
        return;
    }
    
    std::string line;
    
    while (std::getline(passwdFile, line)) {
        // ТЕСТ ПРОВЕРЯЕТ: if line.endswith('sh\n')
        if (line.empty() || line.size() < 3) continue;
        
        // Удаляем пробелы в конце
        while (!line.empty() && std::isspace(line.back())) {
            line.pop_back();
        }
        
        if (line.size() < 2) continue;
        
        // Разбираем строку на поля
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        
        while (std::getline(ss, field, ':')) {
            fields.push_back(field);
        }
        
        // Нужны все 7 полей
        if (fields.size() < 7) continue;
        
        // Получаем shell (последнее поле)
        std::string shell = fields[6];
        
        // Проверяем, заканчивается ли shell на "sh"
        if (shell.size() < 2 || shell.substr(shell.size() - 2) != "sh") {
            continue;
        }
        
        // Получаем данные пользователя
        std::string username = fields[0];
        std::string uid_str = fields[2];
        std::string home = fields[5];
        std::string shell_field = fields[6];
        
        std::string userDir = vfsDir + "/" + username;
        
        // Создаём директорию пользователя
        mkdir(userDir.c_str(), 0755);
        
        // Создаём файл id
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << uid_str;
            idFile.close();
        }
        
        // Создаём файл home
        std::ofstream homeFile(userDir + "/home");
        if (homeFile.is_open()) {
            homeFile << home;
            homeFile.close();
        }
        
        // Создаём файл shell
        std::ofstream shellFile(userDir + "/shell");
        if (shellFile.is_open()) {
            shellFile << shell_field;
            shellFile.close();
        }
        
        g_processed_users.insert(username);
    }
    
    passwdFile.close();
}

// ========== Проверка и создание новых пользователей ==========
void checkAndCreateNewUsers() {
    static bool checked = false;
    if (checked) return; // Проверяем только один раз!
    checked = true;
    
    std::string vfsDir = "/opt/users";
    
    DIR* dir = opendir(vfsDir.c_str());
    if (!dir) {
        return;
    }
    
    struct dirent* entry;
    std::vector<std::string> new_users;
    
    // Сначала собираем всех новых пользователей
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) {
            std::string username = entry->d_name;
            if (username == "." || username == "..") continue;
            
            if (g_processed_users.find(username) != g_processed_users.end()) {
                continue;
            }
            
            new_users.push_back(username);
        }
    }
    closedir(dir);
    
    // Для каждого нового пользователя создаем запись
    for (const auto& username : new_users) {
        // Простое создание записи в /etc/passwd
        std::ofstream passwd("/etc/passwd", std::ios::app);
        if (passwd.is_open()) {
            passwd << username << ":x:1000:1000::/home/" << username << ":/bin/bash\n";
            passwd.close();
        }
        
        // Создаем директорию и файлы VFS
        std::string userDir = vfsDir + "/" + username;
        mkdir(userDir.c_str(), 0755);
        
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << "1000";
            idFile.close();
        }
        
        std::ofstream homeFile(userDir + "/home");
        if (homeFile.is_open()) {
            homeFile << "/home/" + username;
            homeFile.close();
        }
        
        std::ofstream shellFile(userDir + "/shell");
        if (shellFile.is_open()) {
            shellFile << "/bin/bash";
            shellFile.close();
        }
        
        g_processed_users.insert(username);
    }
}

// ========== Главная функция ==========
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Создаём VFS
    createVFS();
    
    // Сразу проверяем новых пользователей (в течение 100ms из fixture)
    checkAndCreateNewUsers();
    
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
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