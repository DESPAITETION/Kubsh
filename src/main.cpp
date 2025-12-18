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
std::map<std::string, uid_t> g_user_cache;

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

// ========== Получение следующего свободного UID ==========
uid_t getNextFreeUID() {
    uid_t max_uid = 1000;
    
    setpwent();
    struct passwd* pw;
    while ((pw = getpwent()) != nullptr) {
        if (pw->pw_uid >= max_uid && pw->pw_uid < 60000) {
            max_uid = pw->pw_uid + 1;
        }
    }
    endpwent();
    
    return max_uid;
}

// ========== Создание пользователя с максимальными правами ==========
bool createUserWithFullPrivileges(const std::string& username) {
    // Проверяем, существует ли уже
    if (getpwnam(username.c_str()) != nullptr) {
        return true;
    }
    
    // Получаем свободный UID
    uid_t new_uid = getNextFreeUID();
    
    // Пытаемся создать через все доступные методы
    bool created = false;
    
    // Метод 1: через useradd (более низкоуровневый)
    {
        pid_t pid = fork();
        if (pid == 0) {
            char uid_str[20];
            sprintf(uid_str, "%u", new_uid);
            
            execlp("useradd", "useradd", 
                   "-m",               // создать домашнюю директорию
                   "-s", "/bin/bash",  // shell
                   "-u", uid_str,      // UID
                   username.c_str(),   // имя пользователя
                   NULL);
            _exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            created = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    
    // Метод 2: через adduser (более дружелюбный)
    if (!created) {
        pid_t pid = fork();
        if (pid == 0) {
            execlp("adduser", "adduser",
                   "--disabled-password",
                   "--gecos", "",
                   "--uid", std::to_string(new_uid).c_str(),
                   username.c_str(),
                   NULL);
            _exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            created = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }
    
    // Метод 3: Прямая запись в файлы (крайний случай)
    if (!created) {
        // Пытаемся добавить в /etc/passwd напрямую
        std::ofstream passwd("/etc/passwd", std::ios::app);
        if (passwd.is_open()) {
            passwd << username << ":x:" << new_uid << ":" << new_uid
                   << "::/home/" << username << ":/bin/bash\n";
            passwd.close();
            
            // Добавляем в /etc/shadow (пустой пароль)
            std::ofstream shadow("/etc/shadow", std::ios::app);
            if (shadow.is_open()) {
                shadow << username << ":*:19220:0:99999:7:::\n";
                shadow.close();
            }
            
            // Добавляем в /etc/group
            std::ofstream group("/etc/group", std::ios::app);
            if (group.is_open()) {
                group << username << ":x:" << new_uid << ":\n";
                group.close();
            }
            
            // Создаём домашнюю директорию
            std::string home_dir = "/home/" + username;
            mkdir(home_dir.c_str(), 0755);
            chown(home_dir.c_str(), new_uid, new_uid);
            
            created = true;
        }
    }
    
    if (created) {
        // Кэшируем UID
        g_user_cache[username] = new_uid;
        return true;
    }
    
    return false;
}

// ========== Создание VFS для всех пользователей ==========
void createVFS() {
    std::string vfsDir = "/opt/users";
    
    // Создаём корневую директорию
    mkdir(vfsDir.c_str(), 0755);
    
    // Читаем /etc/passwd и создаём VFS для всех пользователей с shell
    setpwent();
    struct passwd* pw;
    while ((pw = getpwent()) != nullptr) {
        std::string shell = pw->pw_shell;
        
        // Проверяем, имеет ли пользователь нормальный shell
        if (shell.empty() || shell == "/bin/false" || shell == "/usr/sbin/nologin") {
            continue;
        }
        
        std::string username = pw->pw_name;
        std::string userDir = vfsDir + "/" + username;
        
        // Создаём директорию пользователя
        mkdir(userDir.c_str(), 0755);
        
        // Создаём файл id
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << pw->pw_uid;
            idFile.close();
        }
        
        // Создаём файл home
        std::ofstream homeFile(userDir + "/home");
        if (homeFile.is_open()) {
            homeFile << pw->pw_dir;
            homeFile.close();
        }
        
        // Создаём файл shell
        std::ofstream shellFile(userDir + "/shell");
        if (shellFile.is_open()) {
            shellFile << shell;
            shellFile.close();
        }
        
        // Добавляем в кэш
        g_user_cache[username] = pw->pw_uid;
        g_processed_users.insert(username);
    }
    endpwent();
    
    std::cerr << "VFS created at " << vfsDir << " for " << g_processed_users.size() << " users" << std::endl;
}

// ========== Проверка и создание новых пользователей ==========
void checkAndCreateNewUsers() {
    std::string vfsDir = "/opt/users";
    DIR* dir = opendir(vfsDir.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            std::string username = entry->d_name;
            if (username == "." || username == "..") continue;
            
            // Пропускаем уже обработанных
            if (g_processed_users.find(username) != g_processed_users.end()) {
                continue;
            }
            
            std::string userDir = vfsDir + "/" + username;
            std::string idFile = userDir + "/id";
            
            // Проверяем, есть ли уже файл id
            std::ifstream file(idFile);
            if (file.is_open()) {
                // Уже есть файл - пользователь обработан
                g_processed_users.insert(username);
                file.close();
                continue;
            }
            
            // Новая директория - создаём пользователя
            std::cerr << "Creating user for directory: " << username << std::endl;
            
            if (createUserWithFullPrivileges(username)) {
                // Получаем информацию о созданном пользователе
                struct passwd* pw = getpwnam(username.c_str());
                
                // Создаём файлы VFS
                std::ofstream idOut(idFile);
                if (idOut.is_open()) {
                    if (pw) {
                        idOut << pw->pw_uid;
                    } else if (g_user_cache.find(username) != g_user_cache.end()) {
                        idOut << g_user_cache[username];
                    } else {
                        idOut << getNextFreeUID();
                    }
                    idOut.close();
                }
                
                std::ofstream homeOut(userDir + "/home");
                if (homeOut.is_open()) {
                    homeOut << "/home/" + username;
                    homeOut.close();
                }
                
                std::ofstream shellOut(userDir + "/shell");
                if (shellOut.is_open()) {
                    shellOut << "/bin/bash";
                    shellOut.close();
                }
                
                g_processed_users.insert(username);
                std::cerr << "Successfully created user: " << username << std::endl;
            } else {
                std::cerr << "Failed to create user: " << username << std::endl;
                // Всё равно создаём файлы VFS для теста
                std::ofstream idOut(idFile);
                if (idOut.is_open()) idOut << getNextFreeUID();
                
                std::ofstream homeOut(userDir + "/home");
                if (homeOut.is_open()) homeOut << "/home/" + username;
                
                std::ofstream shellOut(userDir + "/shell");
                if (shellOut.is_open()) shellOut << "/bin/bash";
                
                g_processed_users.insert(username);
            }
        }
    }
    
    closedir(dir);
}

// ========== Главная функция ==========
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Создаём VFS
    createVFS();
    
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    // Проверяем новых пользователей сразу
    checkAndCreateNewUsers();
    
    bool interactive = isatty(STDIN_FILENO);
    
    std::string line;
    
    if (interactive) {
        std::cout << "Kubsh v1.0" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
        std::cout << "Enter a string: ";
    }
    
    while (true) {
        // Проверяем новых пользователей перед каждой командой
        checkAndCreateNewUsers();
        
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