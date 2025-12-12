#include "VFSManager.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

VFSManager::VFSManager() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    
    usersDir = std::string(home) + "/users";
}

void VFSManager::createVFS() {
    // Создаем директорию ~/users
    if (mkdir(usersDir.c_str(), 0755) != 0) {
        // Игнорируем ошибку если директория уже существует
    }
    
    std::cout << "VFS created at: " << usersDir << std::endl;
}