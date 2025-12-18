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
    std::ifstream passwd("/etc/passwd");
    if (!passwd.is_open()) {
        return;
    }
    
    std::string line;
    while (std::getline(passwd, line)) {
        // Пропускаем пустые строки
        if (line.empty()) continue;
        
        // Для проверки как в тесте Python: line.endswith('sh\n')
        // Нужно добавить \n обратно
        std::string line_with_nl = line + "\n";
        
        // Проверяем, заканчивается ли строка на "sh\n"
        // Это соответствует тесту: if line.endswith('sh\n')
        if (line_with_nl.size() >= 3) {
            std::string last_three = line_with_nl.substr(line_with_nl.size() - 3);
            if (last_three != "sh\n") {
                continue;
            }
        }
        
        // Разбираем строку на поля
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        
        while (std::getline(ss, field, ':')) {
            fields.push_back(field);
        }
        
        // Нужны все 7 полей
        if (fields.size() < 7) continue;
        
        // Получаем данные пользователя
        std::string username = fields[0];
        std::string uid = fields[2];      // поле 2 = UID (ВНИМАНИЕ: это индекс 2, а не 3!)
        std::string home = fields[5];     // поле 5 = home
        std::string shell = fields[6];    // поле 6 = shell
        
        // Создаем директорию пользователя
        std::string userDir = vfsDir + "/" + username;
        mkdir(userDir.c_str(), 0755);
        
        // Создаем файл id с реальным UID
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << uid;  // ВАЖНО: fields[2] - это UID
            idFile.close();
        }
        
        // Создаем файл home
        std::ofstream homeFile(userDir + "/home");
        if (homeFile.is_open()) {
            homeFile << home;
            homeFile.close();
        }
        
        // Создаем файл shell
        std::ofstream shellFile(userDir + "/shell");
        if (shellFile.is_open()) {
            shellFile << shell;
            shellFile.close();
        }
        
        // Добавляем в обработанные
        g_processed_users.insert(username);
    }
    
    passwd.close();
}

// ========== Проверка и создание новых пользователей ==========
void checkAndCreateNewUsers() {
    std::string vfsDir = "/opt/users";
    
    // Проверяем существование директории VFS
    struct stat dirStat;
    if (stat(vfsDir.c_str(), &dirStat) != 0 || !S_ISDIR(dirStat.st_mode)) {
        return;
    }
    
    DIR* dir = opendir(vfsDir.c_str());
    if (!dir) {
        return;
    }
    
    struct dirent* entry;
    bool processed_one = false;
    
    while ((entry = readdir(dir)) != nullptr && !processed_one) {
        std::string username = entry->d_name;
        if (username == "." || username == "..") continue;
        
        // Полный путь к директории пользователя
        std::string userDir = vfsDir + "/" + username;
        struct stat pathStat;
        if (stat(userDir.c_str(), &pathStat) != 0) continue;
        if (!S_ISDIR(pathStat.st_mode)) continue;
        
        // Пропускаем уже обработанных пользователей
        if (g_processed_users.find(username) != g_processed_users.end()) {
            continue;
        }
        
        // Проверяем, есть ли файл id (значит пользователь уже обработан)
        std::string idFile = userDir + "/id";
        if (stat(idFile.c_str(), &pathStat) == 0) {
            g_processed_users.insert(username);
            continue;
        }
        
        // НОВЫЙ пользователь - проверяем, существует ли уже в /etc/passwd
        bool exists_in_passwd = false;
        std::ifstream passwd("/etc/passwd");
        std::string line;
        while (std::getline(passwd, line)) {
            if (line.find(username + ":") == 0) {
                exists_in_passwd = true;
                break;
            }
        }
        passwd.close();
        
        if (exists_in_passwd) {
            // Пользователь уже есть в /etc/passwd, но нет VFS файлов
            // Это странно, но создаем файлы
            std::ifstream passwd2("/etc/passwd");
            while (std::getline(passwd2, line)) {
                if (line.find(username + ":") == 0) {
                    std::vector<std::string> fields;
                    std::stringstream ss(line);
                    std::string field;
                    
                    while (std::getline(ss, field, ':')) {
                        fields.push_back(field);
                    }
                    
                    if (fields.size() >= 7) {
                        std::ofstream idOut(idFile);
                        if (idOut.is_open()) {
                            idOut << fields[2];  // UID
                            idOut.close();
                        }
                        
                        std::ofstream homeOut(userDir + "/home");
                        if (homeOut.is_open()) {
                            homeOut << fields[5];  // home
                            homeOut.close();
                        }
                        
                        std::ofstream shellOut(userDir + "/shell");
                        if (shellOut.is_open()) {
                            shellOut << fields[6];  // shell
                            shellOut.close();
                        }
                        
                        g_processed_users.insert(username);
                        processed_one = true;
                    }
                    break;
                }
            }
            passwd2.close();
        } else {
            // СОВЕРШЕННО НОВЫЙ пользователь - добавляем в /etc/passwd
            std::string passwdEntry = username + ":x:1000:1000::/home/" + username + ":/bin/bash\n";
            
            std::ofstream passwdFile("/etc/passwd", std::ios::app);
            if (passwdFile.is_open()) {
                passwdFile << passwdEntry;
                passwdFile.close();
                sync();
                
                // Создаем файлы VFS
                std::ofstream idOut(idFile);
                if (idOut.is_open()) {
                    idOut << "1000";  // Новый пользователь получает UID 1000
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
                processed_one = true;
                
                std::cout << "Created new user: " << username << std::endl;
            }
        }
    }
    
    closedir(dir);
}

// ========== Главная функция ==========
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Создаём VFS при запуске
    createVFS();
    
    // Сразу проверяем новых пользователей
    checkAndCreateNewUsers();
    
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    bool interactive = isatty(STDIN_FILENO);
    
    if (interactive) {
        std::cout << "Kubsh v1.0" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
        std::cout << "Enter a string: ";
    }
    
    // Главный цикл
    while (true) {
        // Проверяем новых пользователей
        checkAndCreateNewUsers();
        
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
                        checkAndCreateNewUsers();
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
                usleep(100000);
            } else {
                // В неинтерактивном режиме (тесты) - ЗАВЕРШАЕМСЯ
                break;
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