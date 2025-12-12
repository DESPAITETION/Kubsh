#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <string>
#include <vector>

class CommandParser {
private:
    std::string command;
    std::vector<std::string> args;
    
public:
    CommandParser(const std::string& input);
    std::string getCommand() const { return command; }
    std::vector<std::string> getArgs() const { return args; }
};

#endif