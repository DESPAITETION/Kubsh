#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include <string>
#include <vector>

class HistoryManager {
private:
    std::vector<std::string> history;
    std::string historyFile;
    
    void loadHistory();
    
public:
    HistoryManager();
    void addToHistory(const std::string& command);
    void saveHistory();
    void printHistory() const;
};

#endif