#include "monitor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <wincrypt.h>
#include <ctime>  
#include <functional>



FileMonitor::FileMonitor(const std::string& directoryPath)
    : directoryPath(directoryPath), isMonitoring(false) {
    InitializeCriticalSection(&criticalSection);
}

FileMonitor::~FileMonitor() {
    if (isMonitoring) stopMonitoring();
    DeleteCriticalSection(&criticalSection);
}


void FileMonitor::startMonitoring() {
    if (isMonitoring) return;

    isMonitoring = true;

    monitorThread = std::thread([this]() {
        while (isMonitoring) {
            EnterCriticalSection(&criticalSection);

            for (const auto& entry : fs::directory_iterator(directoryPath)) {
                if (!fs::is_regular_file(entry)) continue;

                std::string filePath = entry.path().string();
                std::string currentHash = computeFileHash(filePath);
                if (currentHash.empty()) {
                    std::cerr << "Failed to hash file: " << filePath << "\n";
                    continue;
                }

                std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

                auto it = fileHashes.find(filePath);
                if (it == fileHashes.end()) {
                    fileHashes[filePath] = currentHash;
                    fileTimestamps[filePath] = currentTime;
                }
                else if (it->second != currentHash) {
                    std::string oldHash = it->second;

                    std::tm tm = {};
                    localtime_s(&tm, &currentTime);
                    std::ostringstream oss;
                    oss << std::put_time(&tm, "%a %b %d %H:%M:%S %Y");
                    std::string timeStr = oss.str();

                    std::cout << "\n[!] Change detected in file: " << filePath << "\n";
                    std::cout << "    Time of Change : " << timeStr << "\n";
                    std::cout << "    Previous Hash  : " << oldHash << "\n";
                    std::cout << "    Current Hash   : " << currentHash << "\n";

                    fileHashes[filePath] = currentHash;
                    fileTimestamps[filePath] = currentTime;

                    backupFile(filePath);


                   
                }
            }

            LeaveCriticalSection(&criticalSection);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        });
}


void FileMonitor::stopMonitoring() {
    if (!isMonitoring) return;

    isMonitoring = false;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    std::cout << "[*] File monitoring stopped.\n";
}

std::string FileMonitor::computeFileHash(const std::string& filePath) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return "";

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return "";

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        if (!CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer), static_cast<DWORD>(file.gcount()), 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return "";
        }
    }

    BYTE hash[32];
    DWORD hashLen = sizeof(hash);
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::ostringstream oss;
    for (DWORD i = 0; i < hashLen; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return oss.str();
}

void FileMonitor::backupFile(const std::string& filePath) {
    
    std::string timestamp = getCurrentTimestamp();

    
    std::string versionedFile = filePath + "_v" + std::to_string(fileVersions[filePath].size() + 1) + "_" + timestamp + ".txt";

  
    fileVersions[filePath].push_back(versionedFile);

   
    std::ifstream src(filePath, std::ios::binary);
  
    std::ofstream dest(versionedFile, std::ios::binary);


    dest << src.rdbuf();

    std::cout << "[+] Backup created: " << versionedFile << "\n";
}

std::string FileMonitor::getCurrentTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buffer[32];
    sprintf_s(buffer, "%04d%02d%02d_%02d%02d%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return std::string(buffer);
}



