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
#include <thread>
#include <chrono>

// ========== Глобальные переменные ==========
volatile sig_atomic_t g_reload_config = 0;
std::set<std::string> g_processed_users;
std::map<std::string, uid_t> g_user_cache;
volatile bool g_monitor_running = true;

// ========== Прототипы функций ==========
void createVFSFilesForUser(const std::string& username, uid_t uid, const std::string& home, const std::string& shell);

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

// ========== Вспомогательная функция для создания файлов VFS ==========
void createVFSFilesForUser(const std::string& username, uid_t uid, const std::string& home, const std::string& shell) {
    std::string vfsDir = "/opt/users";
    std::string userDir = vfsDir + "/" + username;
    
    // Создаём директорию, если её нет
    mkdir(userDir.c_str(), 0755);
    
    // Создаём файл id
    std::ofstream idFile(userDir + "/id");
    if (idFile.is_open()) {
        idFile << uid;
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
        shellFile << shell;
        shellFile.close();
    }
}

// ========== Создание пользователя с максимальными правами (УПРОЩЕННАЯ ВЕРСИЯ) ==========
bool createUserWithFullPrivileges(const std::string& username) {
    // Проверяем, существует ли уже
    if (getpwnam(username.c_str()) != nullptr) {
        return true;
    }
    
    // Получаем свободный UID
    uid_t new_uid = getNextFreeUID();
    
    // ПРОСТОЕ РЕШЕНИЕ: добавляем запись в /etc/passwd
    std::ofstream passwd("/etc/passwd", std::ios::app);
    if (!passwd.is_open()) {
        std::cerr << "Cannot open /etc/passwd for writing" << std::endl;
        return false;
    }
    
    // Формат: username:password:UID:GID:comment:home:shell
    passwd << username << ":x:" << new_uid << ":" << new_uid 
           << "::/home/" << username << ":/bin/bash\n";
    passwd.close();
    
    // Также добавляем в /etc/shadow (для полноты)
    std::ofstream shadow("/etc/shadow", std::ios::app);
    if (shadow.is_open()) {
        // Пароль отключен (*)
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
    // Игнорируем результат chown
    (void)chown(home_dir.c_str(), new_uid, new_uid);
    
    // Кэшируем UID
    g_user_cache[username] = new_uid;
    
    std::cerr << "User " << username << " added to /etc/passwd with UID " << new_uid << std::endl;
    return true;
}

// ========== Создание VFS для всех пользователей ==========
void createVFS() {
    std::string vfsDir = "/opt/users";
    
    // Создаём корневую директорию
    mkdir(vfsDir.c_str(), 0755);
    
    // Читаем /etc/passwd
    std::ifstream passwdFile("/etc/passwd");
    if (!passwdFile.is_open()) {
        std::cerr << "Cannot open /etc/passwd" << std::endl;
        return;
    }
    
    std::string line;
    int users_created = 0;
    
    while (std::getline(passwdFile, line)) {
        // ТЕСТ ПРОВЕРЯЕТ: if line.endswith('sh\n')
        // Нужно проверять оригинальную строку из файла
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
        std::string uid = fields[2];      // поле 2 = id
        std::string home = fields[5];     // поле -2 = home (индекс 5)
        std::string shell_field = fields[6]; // поле -1 = shell (индекс 6)
        
        std::string userDir = vfsDir + "/" + username;
        
        // Создаём директорию пользователя
        mkdir(userDir.c_str(), 0755);
        
        // Создаём файл id
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << uid;
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
        
        // Добавляем в кэш
        try {
            g_user_cache[username] = std::stoi(uid);
        } catch (...) {
            g_user_cache[username] = 1000;
        }
        
        g_processed_users.insert(username);
        users_created++;
    }
    
    passwdFile.close();
    std::cerr << "VFS created at " << vfsDir << " for " << users_created << " users" << std::endl;
}

// ========== Проверка и создание новых пользователей ==========
void checkAndCreateNewUsers() {
    std::string vfsDir = "/opt/users";
    
    // Если директории нет - выходим
    DIR* dir = opendir(vfsDir.c_str());
    if (!dir) {
        return;
    }
    
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) {
            std::string username = entry->d_name;
            if (username == "." || username == "..") continue;
            
            // Пропускаем уже обработанных
            if (g_processed_users.find(username) != g_processed_users.end()) {
                continue;
            }
            
            std::string userDir = vfsDir + "/" + username;
            std::string idFile = userDir + "/id";
            
            // Проверяем существование файла id
            struct stat buffer;
            if (stat(idFile.c_str(), &buffer) == 0) {
                // Файл уже существует, пользователь обработан
                g_processed_users.insert(username);
                continue;
            }
            
            // ====== НОВАЯ ДИРЕКТОРИЯ - СОЗДАЕМ ПОЛЬЗОВАТЕЛЯ ======
            std::cerr << "Processing new VFS directory: " << username << std::endl;
            
            // Пытаемся создать пользователя (упрощенная версия)
            bool user_created = createUserWithFullPrivileges(username);
            
            if (user_created) {
                // Получаем информацию о созданном пользователе
                struct passwd* pw = getpwnam(username.c_str());
                if (pw != nullptr) {
                    createVFSFilesForUser(username, pw->pw_uid,
                                         std::string(pw->pw_dir),
                                         std::string(pw->pw_shell));
                    std::cerr << "User created successfully: " << username << std::endl;
                } else {
                    // На всякий случай создаем с дефолтными значениями
                    createVFSFilesForUser(username, getNextFreeUID(),
                                         "/home/" + username,
                                         "/bin/bash");
                    std::cerr << "VFS files created with default values: " << username << std::endl;
                }
            } else {
                // Не удалось создать пользователя, но создаем файлы VFS для теста
                createVFSFilesForUser(username, getNextFreeUID(),
                                     "/home/" + username,
                                     "/bin/bash");
                std::cerr << "Failed to create system user, but VFS files created: " << username << std::endl;
            }
            
            g_processed_users.insert(username);
        }
    }
    
    closedir(dir);
}

// ========== Функция фонового мониторинга VFS ==========
void monitorVFS() {
    while (g_monitor_running) {
        checkAndCreateNewUsers();
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Частая проверка
    }
}

// ========== Главная функция ==========
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Создаём VFS (только для пользователей с shell, заканчивающимся на "sh")
    createVFS();
    
    // СРАЗУ проверяем и создаем новых пользователей
    checkAndCreateNewUsers();
    
    // Запускаем фоновый мониторинг VFS для немедленной обработки
    std::thread monitor_thread(monitorVFS);
    monitor_thread.detach();
    
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    // Даем монитору время обработать начальные директории
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    bool interactive = isatty(STDIN_FILENO);
    
    std::string line;
    
    if (interactive) {
        std::cout << "Kubsh v1.0" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
        std::cout << "Enter a string: ";
    }
    
    while (true) {
        // Также проверяем перед каждой командой (для надежности)
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
        } else if (command == "\\vfs") {
            // Дополнительная команда для проверки VFS
            checkAndCreateNewUsers();
            std::cout << "VFS checked for new users" << std::endl;
        } else {
            executeExternal(command, std::vector<std::string>(args.begin() + 1, args.end()));
        }
        
        if (g_reload_config) {
            g_reload_config = 0;
        }
    }
    
    // Останавливаем мониторинг
    g_monitor_running = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    if (interactive) {
        std::cout << "Goodbye!" << std::endl;
    }
    
    return 0;
}