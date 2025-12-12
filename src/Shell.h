#ifndef SHELL_H
#define SHELL_H

#include <string>

class Shell {
private:
    std::string prompt;
    bool running;
    
public:
    Shell();
    void run();
};

#endif