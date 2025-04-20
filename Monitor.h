#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <filesystem>
#include <windows.h>
#include <chrono>
#include <ctime>
#include <functional>


namespace fs = std::filesystem;

class FileMonitor {
public:
    FileMonitor(const std::string& directoryPath);
    ~FileMonitor();

    void startMonitoring();
    void stopMonitoring();

 
   



private:

   
  

 

    std::string directoryPath;
    bool isMonitoring;
    std::thread monitorThread;

    std::unordered_map<std::string, std::string> fileHashes;
    std::unordered_map<std::string, std::time_t> fileTimestamps;
    std::unordered_map<std::string, std::string> lastFileHashes;
    std::unordered_map<std::string, std::vector<std::string>> fileVersions;

    CRITICAL_SECTION criticalSection;

    std::string computeFileHash(const std::string& filePath);
    void backupFile(const std::string& filePath);
    std::string getCurrentTimestamp();
};
