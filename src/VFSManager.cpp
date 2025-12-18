#include "VFSManager.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

VFSManager::VFSManager() {
    // Используем /opt/users как в тестах
    usersDir = "/opt/users";
}

void VFSManager::createVFS() {
    // Создаём корневую директорию
    mkdir(usersDir.c_str(), 0755);
    
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
        // Добавляем \n обратно для проверки
        std::string line_with_nl = line + "\n";
        
        // Проверяем, заканчивается ли строка на "sh\n"
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
        
        if (fields.size() < 7) continue;
        
        std::string username = fields[0];
        std::string uid = fields[2];      // поле 2 = UID
        std::string home = fields[5];     // поле 5 = home
        std::string shell = fields[6];    // поле 6 = shell
        
        // Создаем директорию пользователя
        std::string userDir = usersDir + "/" + username;
        mkdir(userDir.c_str(), 0755);
        
        // Создаем файл id с реальным UID
        std::ofstream idFile(userDir + "/id");
        if (idFile.is_open()) {
            idFile << uid;
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
    }
    
    passwd.close();
    
    std::cout << "VFS created at: " << usersDir << std::endl;
}