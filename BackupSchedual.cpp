#include "BackupSchedual.h"
#include <filesystem>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <shlobj.h>

namespace fs = std::filesystem;

BackupSchedual::BackupSchedual(const std::string& backupDirectory, int intervalInMinutes)
    : backupDirectory(backupDirectory), intervalInMinutes(intervalInMinutes), isRunning(false) {}

BackupSchedual::~BackupSchedual() {
    if (isRunning) {
        stopBackupSchedual();
    }
}

void BackupSchedual::stopBackupSchedual() {
    if (isRunning) {
        isRunning = false;
        if (backupThread.joinable()) {
            backupThread.join();
        }
    }
}

std::string currentDateTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime;
    localtime_s(&localTime, &time);

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S"); // No colons!
    return oss.str();
}


void BackupSchedual::logBackupStatus(const std::string& message) {
    std::ofstream logFile("backup_log.txt", std::ios_base::app);
    if (logFile.is_open()) {
        logFile << currentDateTime() << " - " << message << std::endl;
        logFile.close();
    }
    else {
        std::cerr << "[-] Failed to open log file for writing.\n";
    }
}

void BackupSchedual::backupFiles() {
    try {
        // Generate timestamp folder
        std::string timestamp = currentDateTime();
        std::string backupPath = backupDirectory + "\\" + timestamp;

        if (!fs::exists(backupPath)) {
            fs::create_directories(backupPath);
        }

        for (const auto& entry : fs::directory_iterator("C:\\Users\\user\\source\\repos\\Server-P\\Server-P\\merged")) {
            if (fs::is_regular_file(entry)) {
                std::string sourcePath = entry.path().string();
                std::string destPath = backupPath + "\\" + entry.path().filename().string();
                fs::copy(sourcePath, destPath, fs::copy_options::overwrite_existing);

                std::string fileMsg = "[*] Backup created for: " + entry.path().filename().string();
                std::cout << fileMsg << "\n";
                logBackupStatus(fileMsg);
            }
        }

        std::string completeMsg = "[*] Backup completed in folder: " + timestamp;
        std::cout << completeMsg << "\n";
        logBackupStatus(completeMsg);
    }
    catch (const std::exception& e) {
        std::string errorMsg = std::string("[-] Backup failed: ") + e.what();
        std::cerr << errorMsg << "\n";
        logBackupStatus(errorMsg);
    }
}


void BackupSchedual::backupLoop() {
    while (isRunning) {
        backupFiles();
        std::this_thread::sleep_for(std::chrono::minutes(intervalInMinutes));
    }
}
void BackupSchedual::startBackupSchedual() {
    if (!isRunning) {
        isRunning = true;
        backupFiles();

        backupThread = std::thread(&BackupSchedual::backupLoop, this);
    }
}
