#include "VFSManager.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <dirent.h>
#include <cstdlib>
#include <cstring>

VFSManager::VFSManager() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    usersDir = std::string(home) + "/users";
}

void VFSManager::createVFS() {
    // Создаем основную директорию
    mkdir(usersDir.c_str(), 0755);
    
    std::ifstream passwd("/etc/passwd");
    if (!passwd.is_open()) {
        std::cerr << "Cannot open /etc/passwd" << std::endl;
        return;
    }
    
    std::string line;
    int userCount = 0;
    
    while (std::getline(passwd, line)) {
        if (line.empty()) continue;
        
        // Пропускаем системные пользователи без шелла
        if (line.find(":/usr/sbin/nologin") != std::string::npos ||
            line.find(":/bin/false") != std::string::npos ||
            line.find(":/sbin/nologin") != std::string::npos) {
            continue;
        }
        
        // Парсим строку из /etc/passwd
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        
        while (std::getline(ss, field, ':')) {
            fields.push_back(field);
        }
        
        if (fields.size() < 7) continue;
        
        std::string username = fields[0];
        std::string uid = fields[2];
        std::string homeDir = fields[5];
        std::string shell = fields[6];
        
        // Пропускаем системных пользователей с низким UID
        int uid_num = std::stoi(uid);
        if (uid_num < 1000 && uid_num != 0) continue;
        
        // Создаем директорию пользователя
        std::string userDir = usersDir + "/" + username;
        mkdir(userDir.c_str(), 0755);
        
        // Создаем файл с ID пользователя
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << uid;
            idFile.close();
        }
        
        // Создаем файл с домашней директорией
        std::ofstream homeFile(userDir + "/home");
        if (homeFile.is_open()) {
            homeFile << homeDir;
            homeFile.close();
        }
        
        // Создаем файл с шеллом
        std::ofstream shellFile(userDir + "/shell");
        if (shellFile.is_open()) {
            shellFile << shell;
            shellFile.close();
        }
        
        userCount++;
    }
    
    passwd.close();
    std::cout << "VFS created at " << usersDir << " with " << userCount << " users" << std::endl;
}

void VFSManager::checkAndCreateNewUsers() {
    DIR* dir = opendir(usersDir.c_str());
    if (!dir) {
        std::cerr << "Cannot open VFS directory: " << usersDir << std::endl;
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string username = entry->d_name;
        if (username == "." || username == "..") continue;
        
        std::string userDir = usersDir + "/" + username;
        std::string idFile = userDir + "/id";
        
        struct stat pathStat;
        
        // Проверяем, есть ли файл id
        if (stat(idFile.c_str(), &pathStat) == 0) {
            // Файл id существует, проверяем существует ли пользователь в системе
            bool exists = false;
            std::ifstream passwd("/etc/passwd");
            std::string line;
            
            while (std::getline(passwd, line)) {
                if (line.find(username + ":") == 0) {
                    exists = true;
                    break;
                }
            }
            passwd.close();
            
            if (!exists) {
                // Пользователь удален из системы - удаляем из VFS
                std::string cmd = "rm -rf \"" + userDir + "\"";
                system(cmd.c_str());
                std::cout << "Removed VFS entry for deleted user: " << username << std::endl;
                continue;
            }
        }
        
        // Проверяем, существует ли директория пользователя
        if (stat(userDir.c_str(), &pathStat) != 0) continue;
        if (!S_ISDIR(pathStat.st_mode)) continue;
        
        // Проверяем, существует ли пользователь в системе
        bool exists = false;
        std::ifstream passwd("/etc/passwd");
        std::string line;
        
        while (std::getline(passwd, line)) {
            if (line.find(username + ":") == 0) {
                exists = true;
                break;
            }
        }
        passwd.close();
        
        if (!exists) {
            // Новый пользователь - создаем в системе
            std::cout << "Creating new system user: " << username << std::endl;
            
            // Пробуем создать пользователя
            std::string adduserCmd = "sudo adduser --disabled-password --gecos '' " + username + " 2>/dev/null";
            int result = system(adduserCmd.c_str());
            
            if (result != 0) {
                // Пробуем useradd как fallback
                adduserCmd = "sudo useradd -m " + username + " 2>/dev/null";
                system(adduserCmd.c_str());
            }
            
            // Получаем информацию о созданном пользователе
            struct passwd* pw = getpwnam(username.c_str());
            if (pw) {
                // Создаем файлы в VFS
                std::ofstream idOut(idFile);
                if (idOut.is_open()) {
                    idOut << pw->pw_uid;
                    idOut.close();
                }
                
                std::ofstream homeFile(userDir + "/home");
                if (homeFile.is_open()) {
                    homeFile << pw->pw_dir;
                    homeFile.close();
                }
                
                std::ofstream shellFile(userDir + "/shell");
                if (shellFile.is_open()) {
                    shellFile << pw->pw_shell;
                    shellFile.close();
                }
                
                std::cout << "Created user " << username << " with UID " << pw->pw_uid << std::endl;
            }
        }
    }
    
    closedir(dir);
}