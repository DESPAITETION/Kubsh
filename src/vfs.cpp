// vfs.cpp - код FUSE из примера друга
#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <vector>
#include <string>

static int vfs_pid = -1;

struct UserInfo {
    std::string name;
    uid_t uid;
    std::string home;
    std::string shell;
};

static std::vector<UserInfo> users_cache;

void update_users_cache() {
    users_cache.clear();
    
    struct passwd *pw;
    setpwent();
    while ((pw = getpwent()) != NULL) {
        if (pw->pw_shell != nullptr) {
            std::string shell = pw->pw_shell;
            if (shell.length() >= 2 && shell.substr(shell.length() - 2) == "sh") {
                UserInfo user;
                user.name = pw->pw_name;
                user.uid = pw->pw_uid;
                user.home = pw->pw_dir;
                user.shell = pw->pw_shell;
                users_cache.push_back(user);
            }
        }
    }
    endpwent();
}

static int users_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));
    
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }
    
    std::string pathstr = path;
    if (pathstr[0] == '/') pathstr = pathstr.substr(1);
    
    size_t slash_pos = pathstr.find('/');
    std::string username = (slash_pos == std::string::npos) ? pathstr : pathstr.substr(0, slash_pos);
    
    const UserInfo* user = nullptr;
    for (const auto& u : users_cache) {
        if (u.name == username) {
            user = &u;
            break;
        }
    }
    
    if (!user) return -ENOENT;
    
    if (slash_pos == std::string::npos) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }
    
    std::string prop = pathstr.substr(slash_pos + 1);
    if (prop == "id" || prop == "home" || prop == "shell") {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        
        if (prop == "id") {
            stbuf->st_size = std::to_string(user->uid).length();
        } else if (prop == "home") {
            stbuf->st_size = user->home.length();
        } else if (prop == "shell") {
            stbuf->st_size = user->shell.length();
        }
        return 0;
    }
    
    return -ENOENT;
}

static int users_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;
    
    if (strcmp(path, "/") == 0) {
        filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
        filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);
        
        for (const auto& user : users_cache) {
            filler(buf, user.name.c_str(), NULL, 0, FUSE_FILL_DIR_PLUS);
        }
        return 0;
    }
    
    std::string pathstr = path;
    if (pathstr[0] == '/') pathstr = pathstr.substr(1);
    
    bool found = false;
    for (const auto& user : users_cache) {
        if (user.name == pathstr) {
            found = true;
            break;
        }
    }
    
    if (found) {
        filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);
        filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS);
        filler(buf, "id", NULL, 0, FUSE_FILL_DIR_PLUS);
        filler(buf, "home", NULL, 0, FUSE_FILL_DIR_PLUS);
        filler(buf, "shell", NULL, 0, FUSE_FILL_DIR_PLUS);
        return 0;
    }
    
    return -ENOENT;
}

static int users_open(const char *path, struct fuse_file_info *fi) {
    std::string pathstr = path;
    if (pathstr[0] == '/') pathstr = pathstr.substr(1);
    
    size_t slash_pos = pathstr.find('/');
    if (slash_pos == std::string::npos) return -EISDIR;
    
    std::string username = pathstr.substr(0, slash_pos);
    std::string prop = pathstr.substr(slash_pos + 1);
    
    if (prop != "id" && prop != "home" && prop != "shell") {
        return -ENOENT;
    }
    
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;
    }
    
    return 0;
}

static int users_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi) {
    (void) fi;
    
    std::string pathstr = path;
    if (pathstr[0] == '/') pathstr = pathstr.substr(1);
    
    size_t slash_pos = pathstr.find('/');
    if (slash_pos == std::string::npos) return -EISDIR;
    
    std::string username = pathstr.substr(0, slash_pos);
    std::string prop = pathstr.substr(slash_pos + 1);
    
    const UserInfo* user = nullptr;
    for (const auto& u : users_cache) {
        if (u.name == username) {
            user = &u;
            break;
        }
    }
    
    if (!user) return -ENOENT;
    
    std::string content;
    if (prop == "id") {
        content = std::to_string(user->uid);
    } else if (prop == "home") {
        content = user->home;
    } else if (prop == "shell") {
        content = user->shell;
    } else {
        return -ENOENT;
    }
    
    size_t len = content.length();
    if (offset < (off_t)len) {
        if (offset + size > len) {
            size = len - offset;
        }
        memcpy(buf, content.c_str() + offset, size);
    } else {
        size = 0;
    }
    
    return size;
}

static int users_mkdir(const char *path, mode_t mode) {
    (void) mode;
    
    std::string pathstr = path;
    if (pathstr[0] == '/') pathstr = pathstr.substr(1);
    
    if (pathstr.find('/') != std::string::npos) {
        return -EACCES;
    }
    
    std::string username = pathstr;
    
    struct passwd *pw = getpwnam(username.c_str());
    if (pw != NULL) {
        update_users_cache();
        return 0;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        
        execlp("useradd", "useradd", "-m", username.c_str(), (char*)NULL);
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            update_users_cache();
            return 0;
        }
        
        return -EIO;
    }
    
    return -EIO;
}

static int users_rmdir(const char *path) {
    std::string pathstr = path;
    if (pathstr[0] == '/') pathstr = pathstr.substr(1);
    
    if (pathstr.find('/') != std::string::npos) {
        return -EACCES;
    }
    
    std::string username = pathstr;
    
    struct passwd *pw = getpwnam(username.c_str());
    if (pw == NULL) {
        return -ENOENT;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        
        execlp("userdel", "userdel", "-r", username.c_str(), (char*)NULL);
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            update_users_cache();
            return 0;
        }
        
        return -EIO;
    }
    
    return -EIO;
}

static struct fuse_operations users_oper{};

void init_fuse_operations() {
    users_oper.getattr = users_getattr;
    users_oper.mkdir = users_mkdir;
    users_oper.rmdir = users_rmdir;
    users_oper.open = users_open;
    users_oper.read = users_read;
    users_oper.readdir = users_readdir;
}

extern "C" int start_users_vfs(const char *mount_point) {
    int pid = fork();
    if (pid == 0) {
        char *fuse_argv[] = {
            (char*)"users_vfs",
            (char*)"-f",
            (char*)"-s",
            (char*)mount_point,
            NULL
        };
        
        init_fuse_operations();
        update_users_cache();
        
        int ret = fuse_main(4, fuse_argv, &users_oper, NULL);
        exit(ret);
    } else if (pid > 0) {
        vfs_pid = pid;
        usleep(100000);
        return 0;
    }
    
    return -1;
}

extern "C" void stop_users_vfs() {
    if (vfs_pid != -1) {
        kill(vfs_pid, SIGTERM);
        waitpid(vfs_pid, NULL, 0);
        vfs_pid = -1;
    }
}