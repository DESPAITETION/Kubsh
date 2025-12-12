#include "HistoryManager.h"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <pwd.h>

HistoryManager::HistoryManager() {
    // Определяем путь к домашней директории
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    
    historyFile = std::string(home) + "/.kubsh_history";
    loadHistory();
}

void HistoryManager::loadHistory() {
    std::ifstream file(historyFile);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line) && history.size() < 1000) {
        if (!line.empty()) {
            history.push_back(line);
        }
    }
    
    file.close();
}

void HistoryManager::addToHistory(const std::string& command) {
    if (command.empty() || command == "\\q") return;
    
    history.push_back(command);
    if (history.size() > 1000) {
        history.erase(history.begin());
    }
}

void HistoryManager::saveHistory() {
    std::ofstream file(historyFile);
    if (!file.is_open()) {
        std::cerr << "Cannot save history to " << historyFile << std::endl;
        return;
    }
    
    for (const auto& cmd : history) {
        file << cmd << std::endl;
    }
    
    file.close();
}

void HistoryManager::printHistory() const {
    for (size_t i = 0; i < history.size(); ++i) {
        std::cout << i + 1 << ": " << history[i] << std::endl;
    }
}