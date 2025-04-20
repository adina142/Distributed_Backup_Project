#ifndef BACKUPSCHEDUAL_H
#define BACKUPSCHEDUAL_H

#include <string>
#include <thread>
#include <fstream>

class BackupSchedual {
public:
    BackupSchedual(const std::string& backupDirectory, int intervalInMinutes);
    ~BackupSchedual();

    void startBackupSchedual();
    void stopBackupSchedual();

private:
    std::string backupDirectory;
    int intervalInMinutes;
    bool isRunning;
    std::thread backupThread;

    void backupLoop();
    void backupFiles();
    void logBackupStatus(const std::string& message);
};

#endif 
