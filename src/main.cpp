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
#include <sys/inotify.h>
#include <unordered_set>
#include <thread>
#include <chrono>

// ========== Глобальные переменные ==========
volatile sig_atomic_t g_reload_config = 0;
volatile sig_atomic_t g_running = 1;
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

// ========== Создание пользователя через adduser ==========
bool createUser(const std::string& username) {
    // Проверяем, существует ли уже пользователь
    struct passwd* pw = getpwnam(username.c_str());
    if (pw != nullptr) {
        return true;
    }
    
    // Создаём пользователя
    pid_t pid = fork();
    if (pid == 0) {
        // В дочернем процессе
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

// ========== Инициализация VFS ==========
void initVFS() {
    std::string vfsDir = "/opt/users";
    
    // Создаём корневую директорию VFS
    mkdir(vfsDir.c_str(), 0755);
    
    // Создаём VFS для существующих пользователей
    std::ifstream passwd("/etc/passwd");
    if (passwd.is_open()) {
        std::string line;
        while (std::getline(passwd, line)) {
            // Проверяем, имеет ли пользователь shell
            if (line.find(":/bin/") == std::string::npos && 
                line.find(":/usr/bin/") == std::string::npos) {
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
                
                // Создаём файлы VFS
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
}

// ========== Мониторинг VFS для создания пользователей ==========
void monitorVFS() {
    std::string vfsDir = "/opt/users";
    
    // Используем inotify для отслеживания изменений
    int fd = inotify_init();
    if (fd < 0) return;
    
    int wd = inotify_add_watch(fd, vfsDir.c_str(), IN_CREATE | IN_MOVED_TO);
    if (wd < 0) {
        close(fd);
        return;
    }
    
    char buffer[4096];
    
    while (g_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        
        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        
        if (ret > 0 && FD_ISSET(fd, &fds)) {
            int length = read(fd, buffer, sizeof(buffer));
            if (length < 0) break;
            
            int i = 0;
            while (i < length) {
                struct inotify_event* event = (struct inotify_event*)&buffer[i];
                
                if (event->len && (event->mask & IN_CREATE || event->mask & IN_MOVED_TO)) {
                    std::string dirname = event->name;
                    
                    // Проверяем, что это директория (тест создаёт именно директории)
                    std::string fullPath = vfsDir + "/" + dirname;
                    struct stat st;
                    if (stat(fullPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                        // Пытаемся создать пользователя
                        if (createUser(dirname)) {
                            // Создаём файлы VFS
                            struct passwd* pw = getpwnam(dirname.c_str());
                            if (pw != nullptr) {
                                std::ofstream idFile(fullPath + "/id");
                                if (idFile.is_open()) {
                                    idFile << pw->pw_uid;
                                    idFile.close();
                                }
                                
                                std::ofstream homeFile(fullPath + "/home");
                                if (homeFile.is_open()) {
                                    homeFile << pw->pw_dir;
                                    homeFile.close();
                                }
                                
                                std::ofstream shellFile(fullPath + "/home");
                                if (shellFile.is_open()) {
                                    shellFile << pw->pw_shell;
                                    shellFile.close();
                                }
                            }
                        }
                    }
                }
                
                i += sizeof(struct inotify_event) + event->len;
            }
        }
        
        // Краткая пауза
        usleep(50000); // 50ms
    }
    
    inotify_rm_watch(fd, wd);
    close(fd);
}

// ========== Запуск мониторинга в отдельном потоке ==========
void startVFSMonitor() {
    std::thread monitorThread([]() {
        // Даём время на инициализацию
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        monitorVFS();
    });
    monitorThread.detach();
}

// ========== Главная функция ==========
int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    // Инициализируем VFS
    initVFS();
    
    // Запускаем мониторинг VFS в отдельном потоке
    startVFSMonitor();
    
    // Настраиваем обработчик сигналов
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    // Основной цикл шелла
    bool interactive = isatty(STDIN_FILENO);
    
    std::string line;
    
    if (interactive) {
        std::cout << "Kubsh v1.0" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
        std::cout << "Enter a string: ";
    }
    
    // Для тестов - даём время на создание VFS
    if (!interactive) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
        
        // Для тестов - небольшая пауза
        if (!interactive) {
            usleep(10000); // 10ms
        }
    }
    
    g_running = 0;
    
    if (interactive) {
        std::cout << "Goodbye!" << std::endl;
    }
    
    return 0;
}