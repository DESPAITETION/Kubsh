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
#include <set>

VFSManager::VFSManager() {
    usersDir = "/opt/users";
}

void VFSManager::createVFS() {
    mkdir(usersDir.c_str(), 0755);
    
    std::ifstream passwd("/etc/passwd");
    if (!passwd.is_open()) {
        return;
    }
    
    std::string line;
    while (std::getline(passwd, line)) {
        if (line.empty()) continue;
        
        std::string line_with_nl = line + "\n";
        if (line_with_nl.size() < 3) continue;
        std::string last_three = line_with_nl.substr(line_with_nl.size() - 3);
        if (last_three != "sh\n") {
            continue;
        }
        
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        
        while (std::getline(ss, field, ':')) {
            fields.push_back(field);
        }
        
        if (fields.size() < 7) continue;
        
        std::string username = fields[0];
        std::string uid = fields[2];
        std::string home = fields[5];
        std::string shell = fields[6];
        
        std::string userDir = usersDir + "/" + username;
        mkdir(userDir.c_str(), 0755);
        
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << uid;
            idFile.close();
        }
        
        std::ofstream homeFile(userDir + "/home");
        if (homeFile.is_open()) {
            homeFile << home;
            homeFile.close();
        }
        
        std::ofstream shellFile(userDir + "/shell");
        if (shellFile.is_open()) {
            shellFile << shell;
            shellFile.close();
        }
    }
    
    passwd.close();
    
    std::cout << "VFS created at: " << usersDir << std::endl;
}

void VFSManager::checkAndCreateNewUsers() {
    DIR* dir = opendir(usersDir.c_str());
    if (!dir) {
        return;
    }
    
    struct dirent* entry;
    
    // Собираем существующих пользователей
    std::set<std::string> existingUsers;
    std::ifstream passwd("/etc/passwd");
    std::string line;
    while (std::getline(passwd, line)) {
        if (!line.empty() && line.find(':') != std::string::npos) {
            existingUsers.insert(line.substr(0, line.find(':')));
        }
    }
    passwd.close();
    
    // Проверяем все директории
    while ((entry = readdir(dir)) != nullptr) {
        std::string username = entry->d_name;
        if (username == "." || username == "..") continue;
        
        std::string userDir = usersDir + "/" + username;
        struct stat pathStat;
        if (stat(userDir.c_str(), &pathStat) != 0) continue;
        if (!S_ISDIR(pathStat.st_mode)) continue;
        
        // Проверяем файл id
        std::string idFile = userDir + "/id";
        if (stat(idFile.c_str(), &pathStat) == 0) continue;
        
        // Проверяем существование пользователя
        if (existingUsers.find(username) != existingUsers.end()) {
            // Пользователь существует в /etc/passwd
            // Нужно создать файлы VFS для него
            std::ifstream passwd2("/etc/passwd");
            std::string line2;
            while (std::getline(passwd2, line2)) {
                if (line2.find(username + ":") == 0) {
                    std::vector<std::string> fields;
                    std::stringstream ss(line2);
                    std::string field;
                    
                    while (std::getline(ss, field, ':')) {
                        fields.push_back(field);
                    }
                    
                    if (fields.size() >= 7) {
                        std::ofstream idOut(idFile);
                        if (idOut.is_open()) {
                            idOut << fields[2];
                            idOut.close();
                        }
                        
                        std::ofstream homeOut(userDir + "/home");
                        if (homeOut.is_open()) {
                            homeOut << fields[5];
                            homeOut.close();
                        }
                        
                        std::ofstream shellOut(userDir + "/shell");
                        if (shellOut.is_open()) {
                            shellOut << fields[6];
                            shellOut.close();
                        }
                    }
                    break;
                }
            }
            passwd2.close();
        } else {
            // НОВЫЙ пользователь - добавляем в /etc/passwd
            std::string passwdEntry = username + ":x:1000:1000::/home/" + username + ":/bin/bash\n";
            
            std::ofstream passwdFile("/etc/passwd", std::ios::app);
            if (passwdFile.is_open()) {
                passwdFile << passwdEntry;
                passwdFile.close();
                sync();
                
                // Создаем файлы VFS
                std::ofstream idOut(idFile);
                if (idOut.is_open()) {
                    idOut << "1000";
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
            }
        }
    }
    
    closedir(dir);
}