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

VFSManager::VFSManager() {
    usersDir = "/opt/users";
}

void VFSManager::createVFS() {
    mkdir(usersDir.c_str(), 0755);
    
    std::ifstream passwd("/etc/passwd");
    if (!passwd.is_open()) return;
    
    std::string line;
    while (std::getline(passwd, line)) {
        if (line.empty()) continue;
        
        std::string line_with_nl = line + "\n";
        if (line_with_nl.size() < 3) continue;
        if (line_with_nl.substr(line_with_nl.size() - 3) != "sh\n") continue;
        
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, ':')) fields.push_back(field);
        if (fields.size() < 7) continue;
        
        std::string userDir = usersDir + "/" + fields[0];
        mkdir(userDir.c_str(), 0755);
        
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << fields[2];
            idFile.close();
        }
        
        std::ofstream homeFile(userDir + "/home");
        if (homeFile.is_open()) {
            homeFile << fields[5];
            homeFile.close();
        }
        
        std::ofstream shellFile(userDir + "/shell");
        if (shellFile.is_open()) {
            shellFile << fields[6];
            shellFile.close();
        }
    }
    
    passwd.close();
    std::cout << "VFS created at: " << usersDir << std::endl;
}

void VFSManager::checkAndCreateNewUsers() {
    DIR* dir = opendir(usersDir.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string username = entry->d_name;
        if (username == "." || username == "..") continue;
        
        std::string userDir = usersDir + "/" + username;
        std::string idFile = userDir + "/id";
        
        struct stat pathStat;
        if (stat(idFile.c_str(), &pathStat) == 0) continue;
        if (stat(userDir.c_str(), &pathStat) != 0) continue;
        if (!S_ISDIR(pathStat.st_mode)) continue;
        
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
            std::string passwdEntry = username + ":x:1000:1000::/home/" + username + ":/bin/bash\n";
            std::ofstream passwdFile("/etc/passwd", std::ios::app);
            if (passwdFile.is_open()) {
                passwdFile << passwdEntry;
                passwdFile.close();
                sync();  // Синхронизация
                
                std::ofstream idOut(idFile);
                if (idOut.is_open()) {
                    idOut << "1000";
                    idOut.close();
                }
            }
            break;
        }
    }
    
    closedir(dir);
}