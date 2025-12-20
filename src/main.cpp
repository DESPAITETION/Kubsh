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
#include "VFSManager.h"

volatile sig_atomic_t g_reload_config = 0;

void handleSIGHUP(int sig) {
    (void)sig;
    std::cout << "\nConfiguration reloaded" << std::endl;
    g_reload_config = 1;
}

std::vector<std::string> parseCommand(const std::string& input) {
    std::vector<std::string> args;
    std::stringstream ss(input);
    std::string token;
    
    while (ss >> token) {
        args.push_back(token);
    }
    
    return args;
}

void printCommandNotFound(const std::string& cmd) {
    std::cout << cmd << ": command not found" << std::endl;
}

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

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    VFSManager vfsManager;
    vfsManager.createVFS();
    
    struct sigaction sa;
    sa.sa_handler = handleSIGHUP;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
    
    bool interactive = isatty(STDIN_FILENO);
    
    if (interactive) {
        std::cout << "Kubsh v1.0" << std::endl;
        std::cout << "Type '\\q' to exit, Ctrl+D to exit" << std::endl;
    }
    
    // ОДНА проверка при запуске
    vfsManager.checkAndCreateNewUsers();
    
    while (true) {
        if (interactive) {
            std::cout << "$ ";
        }
        
        std::string line;
        if (std::getline(std::cin, line)) {
            if (line == "\\q") {
                break;
            } else if (!line.empty()) {
                std::vector<std::string> args = parseCommand(line);
                if (!args.empty()) {
                    std::string command = args[0];
                    
                    if (command == "echo" || command == "debug") {
                        executeEcho(args);
                    } else {
                        executeExternal(command, std::vector<std::string>(args.begin() + 1, args.end()));
                    }
                }
            }
        } else {
            break;  // EOF - выходим
        }
        
        if (g_reload_config) {
            g_reload_config = 0;
        }
    }
    
    return 0;
}