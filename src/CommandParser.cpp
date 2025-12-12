#include "CommandParser.h"
#include <sstream>

CommandParser::CommandParser(const std::string& input) {
    std::stringstream ss(input);
    std::string token;
    bool first = true;
    
    while (ss >> token) {
        if (first) {
            command = token;
            first = false;
        } else {
            args.push_back(token);
        }
    }
}