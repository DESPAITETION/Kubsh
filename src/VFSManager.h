#ifndef VFS_MANAGER_H
#define VFS_MANAGER_H

#include <string>

class VFSManager {
private:
    std::string usersDir;
    
public:
    VFSManager();
    void createVFS();
    void checkAndCreateNewUsers();  // Без параметра
};

#endif