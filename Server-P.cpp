#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <fstream>
#include <string>
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <wincrypt.h>
#include <limits>
#include "monitor.h" // Include your file monitor header
#include "BackupSchedual.h"
#pragma comment(lib, "Ws2_32.lib")
#define PORT 8080
#define BUFFER_SIZE 1024
#define EXPECTED_CHUNKS 4
int expectedChunks = EXPECTED_CHUNKS;
std::atomic<int> clientCounter(1);
std::mutex fileMapMutex;
sockaddr_in clientAddr;
int clientAddrLen = sizeof(clientAddr);
#define REPLICA1_IP "127.0.0.1"
#define REPLICA1_PORT 9090

#define REPLICA2_IP "127.0.0.1"
#define REPLICA2_PORT 9091
#define AUTH_PASSWORD "backup123"


struct FileTracker {
    int totalChunks = 0;
    std::atomic<int> receivedChunks{ 0 };
};

std::map<std::string, FileTracker> fileChunkMap;


std::map<std::string, std::vector<std::string>> validTokens = {
    {"mySecret123", {"html.txt"}},
    {"myNew123", {"ALGO.pdf"}},
    {"adminToken", {"*"}}
};

void logError(const std::string& errorMsg, int errorCode = 0) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);

    std::ostringstream timeStream;
    timeStream << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");

    std::cerr << "[" << timeStream.str() << "] ERROR: " << errorMsg;
    if (errorCode != 0) {
        std::cerr << " | Error Code: " << errorCode;
    }
    std::cerr << std::endl;
}

bool isAuthorized(const std::string& token, const std::string& filename) {
    auto it = validTokens.find(token);
    if (it == validTokens.end()) return false;

    const std::vector<std::string>& allowedFiles = it->second;
    return std::find(allowedFiles.begin(), allowedFiles.end(), filename) != allowedFiles.end()
        || std::find(allowedFiles.begin(), allowedFiles.end(), "*") != allowedFiles.end();
}

void ensureDirectory(const std::string& dirName) {
    std::error_code ec;
    std::filesystem::create_directories(dirName, ec);
    if (ec) {
        std::cerr << "[-] Failed to create directory: " << dirName << " | " << ec.message() << "\n";
    }
}
bool restoreChunksFromReplica(const std::string& filename, int totalChunks) {
    ensureDirectory("temp_chunks");
    bool allRestored = true;

    for (int i = 1; i <= totalChunks; ++i) {
        std::string replicaPath = "replica/" + filename + "_chunk" + std::to_string(i) + ".txt";
        std::string tempPath = "temp_chunks/" + filename + "_chunk" + std::to_string(i) + ".txt";

        if (std::filesystem::exists(replicaPath)) {
            std::ifstream in(replicaPath, std::ios::binary);
            std::ofstream out(tempPath, std::ios::binary);
            if (!in || !out) {
                logError("Failed to restore chunk: " + std::to_string(i), errno);
                allRestored = false;
                continue;
            }

            out << in.rdbuf();
            in.close();
            out.close();

            std::lock_guard<std::mutex> lock(fileMapMutex);
            fileChunkMap[filename].receivedChunks++;
            std::cout << "[+] Restored chunk " << i << " from replica.\n";
        }
        else {
            logError("Missing replica chunk: " + replicaPath);
            allRestored = false;
        }
    }

    std::string replicaMergedPath = "replica_merged/" + filename;
    std::string mergedPath = "merged/" + filename;

    if (std::filesystem::exists(replicaMergedPath)) {
        ensureDirectory("merged");
        try {
            std::filesystem::copy_file(replicaMergedPath, mergedPath, std::filesystem::copy_options::overwrite_existing);
            std::cout << "[+] Restored merged file from replica_merged: " << filename << "\n";
        }
        catch (const std::filesystem::filesystem_error& e) {
            logError("Failed to restore merged file: " + std::string(e.what()));
            allRestored = false;
        }
    }
    else {
        std::cout << "[!] No merged file found for: " << filename << "\n";
    }

    return allRestored;
}
bool sendFileToReplica(const std::string& filePath, const std::string& serverIP, int serverPort) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        logError("Failed to open file: " + filePath);
        return false;
    }
    // Extract filename from filePath
    std::string fileName = std::filesystem::path(filePath).filename().string();

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        logError("Socket creation failed.");
        return false;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        logError("Connection to replica server failed.", WSAGetLastError());
        closesocket(sock);
        return false;
    }



    uint32_t nameLen = htonl(fileName.size());
    if (send(sock, (char*)&nameLen, sizeof(nameLen), 0) == SOCKET_ERROR ||
        send(sock, fileName.c_str(), fileName.size(), 0) == SOCKET_ERROR) {
        std::cerr << "[-] Failed to send filename.\n";
        closesocket(sock);
        return false;
    }


    file.seekg(0, std::ios::end);
    uint32_t fileSize = htonl(file.tellg());
    file.seekg(0, std::ios::beg);
    if (send(sock, (char*)&fileSize, sizeof(fileSize), 0) == SOCKET_ERROR) {
        std::cerr << "[-] Failed to send file size.\n";
        closesocket(sock);
        return false;
    }


    char buffer[BUFFER_SIZE];
    while (!file.eof()) {
        file.read(buffer, BUFFER_SIZE);
        int bytesRead = file.gcount();
        if (send(sock, buffer, bytesRead, 0) == SOCKET_ERROR) {
            std::cerr << "[-] Failed to send file data.\n";
            closesocket(sock);
            return false;
        }
    }

    std::cout << "[+] File sent to replica server: " << serverIP << ":" << serverPort << "\n";
    closesocket(sock);
    return true;
}


void mergeChunksIfComplete(const std::string& filename) {
    std::lock_guard<std::mutex> lock(fileMapMutex);

    FileTracker& tracker = fileChunkMap[filename];
    if (tracker.receivedChunks != tracker.totalChunks)
        return;

    std::string mergedPath = "merged/" + filename;
    std::ofstream outFile(mergedPath, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "[-] Failed to open output file for merging: " << mergedPath << "\n";
        return;
    }

    for (int i = 1; i <= tracker.totalChunks; ++i) {
        std::string chunkPath = "temp_chunks/" + filename + "_chunk" + std::to_string(i) + ".txt";
        std::ifstream inFile(chunkPath, std::ios::binary);
        if (!inFile.is_open()) {
            std::cerr << "[-] Missing chunk: " << chunkPath << "\n";
            outFile.close();
            return;
        }

        outFile << inFile.rdbuf();
        inFile.close();
    }

    outFile.close();
    std::cout << "[+] All chunks received. File merged: " << mergedPath << "\n";

    try {
        std::filesystem::create_directories("replica_merged");
        std::filesystem::copy_file(
            mergedPath,
            "replica_merged/" + filename,
            std::filesystem::copy_options::overwrite_existing
        );
        std::cout << "[+] Replica of merged file stored at: replica_merged/" << filename << "\n";
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[-] Failed to store replica of merged file: " << e.what() << "\n";
    }
    // Now send the merged file to replica servers
    std::cout << "[*] Sending merged file to replica server: " << REPLICA1_IP << ":" << REPLICA1_PORT << "\n";
    sendFileToReplica(mergedPath, REPLICA1_IP, REPLICA1_PORT);

    std::cout << "[*] Sending merged file to replica server: " << REPLICA2_IP << ":" << REPLICA2_PORT << "\n";
    sendFileToReplica(mergedPath, REPLICA2_IP, REPLICA2_PORT);


    
}



void deleteFileWithRestore(const std::string& filename) {
    std::vector<std::string> deletedChunks;
    int totalChunks = 0;

    {
        std::lock_guard<std::mutex> lock(fileMapMutex);
        if (fileChunkMap.find(filename) == fileChunkMap.end()) {
            std::cout << "[-] File not found in chunk map.\n";
            return;
        }
        totalChunks = fileChunkMap[filename].totalChunks;
    }


    for (int i = 1; i <= totalChunks; ++i) {
        std::string chunkPath = "temp_chunks/" + filename + "_chunk" + std::to_string(i) + ".txt";
        if (std::filesystem::exists(chunkPath)) {
            std::filesystem::remove(chunkPath);
            deletedChunks.push_back(chunkPath);
        }
    }

    std::cout << "[*] Chunks deleted from temp_chunks for file: " << filename << "\n";


    std::string mergedFilePath = "merged/" + filename;
    if (std::filesystem::exists(mergedFilePath)) {
        std::filesystem::remove(mergedFilePath);
        std::cout << "[*] Merged file deleted: " << mergedFilePath << "\n";
    }
    else {
        std::cout << "[-] Merged file not found: " << mergedFilePath << "\n";
    }

    std::cout << "[?] Do you want to restore the file from replica? (y/n): ";

    char response;
    std::cin >> response;

    char ch;
    while (std::cin.get(ch) && ch != '\n') {}
    if (response == 'y' || response == 'Y') {
        std::string restoreFile;
        std::cout << "Enter filename to restore from replica: ";
        std::getline(std::cin, restoreFile);

        int restoreChunks = 0;
        {
            std::lock_guard<std::mutex> lock(fileMapMutex);
            if (fileChunkMap.find(restoreFile) == fileChunkMap.end()) {
                std::cerr << "[-] File not found in chunk map.\n";
                return;
            }
            restoreChunks = fileChunkMap[restoreFile].totalChunks;
        }

        if (restoreChunksFromReplica(restoreFile, restoreChunks)) {
            mergeChunksIfComplete(restoreFile);
        }
        else {
            std::cerr << "[-] Restore incomplete. Some chunks were missing in replica.\n";
        }
    }
}

void handleClient(SOCKET clientSocket) {
    char buffer[BUFFER_SIZE];

    // Step 1: Receive metadata line (terminated with '\n')
    std::string metadataLine;
    int bytesReceived = 0;
    while (metadataLine.find('\n') == std::string::npos) {
        bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        if (bytesReceived <= 0) {
            std::cerr << "[-] Failed to receive metadata.\n";
            closesocket(clientSocket);
            return;
        }
        metadataLine.append(buffer, bytesReceived);
    }

    // Step 2: Split metadata from file content
    size_t newlinePos = metadataLine.find('\n');
    std::string metadata = metadataLine.substr(0, newlinePos);
    std::string leftover = metadataLine.substr(newlinePos + 1); // Remaining data after metadata

    std::cout << "[*] Metadata received: " << metadata << "\n";

    // Step 3: Parse metadata
    std::string filename, token;
    int chunkId = 0, totalChunks = 0;

    size_t pos1 = metadata.find('|');
    size_t pos2 = metadata.find('|', pos1 + 1);
    size_t pos3 = metadata.find('|', pos2 + 1);

    if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos) {
        std::cerr << "[-] Invalid metadata format.\n";
        closesocket(clientSocket);
        return;
    }

    filename = metadata.substr(0, pos1);
    chunkId = std::stoi(metadata.substr(pos1 + 1, pos2 - pos1 - 1));
    totalChunks = std::stoi(metadata.substr(pos2 + 1, pos3 - pos2 - 1));
    token = metadata.substr(pos3 + 1);
    token.erase(token.find_last_not_of(" \r\n\t") + 1); // Trim whitespace

    std::cout << "[*] Extracted Token: " << token << "\n";

    // Step 4: Ensure directories exist
    ensureDirectory("temp_chunks");
    ensureDirectory("replica");
    ensureDirectory("merged");

    std::string chunkPath = "temp_chunks/" + filename + "_chunk" + std::to_string(chunkId) + ".txt";
    std::string replicaPath = "replica/" + filename + "_chunk" + std::to_string(chunkId) + ".txt";

    std::ofstream chunkFile(chunkPath, std::ios::binary);
    std::ofstream replicaFile(replicaPath, std::ios::binary);

    if (!chunkFile.is_open() || !replicaFile.is_open()) {
        std::cerr << "[-] Failed to open chunk/replica files.\n";
        closesocket(clientSocket);
        return;
    }

    // Step 5: Write leftover data (in case chunk data began in metadata buffer)
    if (!leftover.empty()) {
        chunkFile.write(leftover.c_str(), leftover.size());
        replicaFile.write(leftover.c_str(), leftover.size());
        std::cout << "[*] Written leftover data: " << leftover.size() << " bytes\n";
    }

    // Step 6: Continue receiving remaining chunk data
    while ((bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        chunkFile.write(buffer, bytesReceived);
        replicaFile.write(buffer, bytesReceived);
    }


    if (bytesReceived == 0) {
        std::cout << "[*] Client disconnected.\n";
    }
    else if (bytesReceived < 0) {
        std::cerr << "[-] Failed to receive data.\n";
    }

    chunkFile.close();
    replicaFile.close();
    closesocket(clientSocket);

    std::cout << "[+] Chunk " << chunkId << " received for " << filename << "\n";

    // Step 7: Update tracker
    {
        std::lock_guard<std::mutex> lock(fileMapMutex);
        FileTracker& tracker = fileChunkMap[filename];
        if (tracker.totalChunks == 0) tracker.totalChunks = totalChunks;
        tracker.receivedChunks++;
    }

    // Step 8: Merge if all chunks received
    mergeChunksIfComplete(filename);
}



int main() {
    FileMonitor monitor("C:\\Users\\user\\source\\repos\\Server-P\\Server-P\\merged");
    bool monitoringStarted = false;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[-] WSAStartup failed.\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "[-] Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[-] Bind failed.\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[-] Listen failed.\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "[*] Server listening on port " << PORT << "\n";

    while (true) {
        std::cout << "\n[MENU] Choose an option:\n";
        std::cout << "1. Wait for client and receive file chunk\n";
        std::cout << "2. Delete file and associated chunks and RESTORE OPTIONALLY\n";
        std::cout << "3. Start Monitoring File Changes.\n";
        std::cout << "4. Stop Monitoring File Changes.\n";
        std::cout << "5. Start Backup Scheduler.\n";
        std::cout << "6. Stop Backup Scheduler.\n";
        std::cout << "7. Exit\n";

        std::cout << "Enter choice: ";

        int choice;
        std::cin >> choice;
        std::cin.ignore(); // flush newline
        BackupSchedual backup("C:\\Users\\user\\source\\repos\\Server-P\\Server-P\\Backup_folder", 1); // Interval in minutes
        if (choice == 1) {
            std::cout << "[*] Waiting for " << expectedChunks << " chunks...\n";
            for (int i = 0; i < expectedChunks; ++i) {
                SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrLen);
                if (clientSocket == INVALID_SOCKET) {
                    std::cerr << "[-] Accept failed.\n";
                    continue;
                }
                handleClient(clientSocket);
            }
        }
        else if (choice == 2) {
            std::string filename;
            std::cout << "Enter filename to delete and optionally restore: ";
            std::getline(std::cin, filename);
            deleteFileWithRestore(filename);
        }


        else if (choice == 3) {
            if (!monitoringStarted) {
                monitor.startMonitoring();  // Start monitoring changes
                monitoringStarted = true;
                std::cout << "[*] File monitoring started.\n";
            }
            else {
                std::cout << "[!] Monitoring is already running.\n";
            }
        }
        else if (choice == 4) {
            if (monitoringStarted) {
                monitor.stopMonitoring();  // Stop monitoring changes
                monitoringStarted = false;
                std::cout << "[*] File monitoring stopped.\n";
            }
            else {
                std::cout << "[!] Monitoring is not currently running.\n";
            }
        }
        else if (choice == 5) {
            backup.startBackupSchedual();
            std::cout << "[*] Backup scheduler started.\n";
        }
        else if (choice == 6) {
            backup.stopBackupSchedual();
            std::cout << "[*] Backup scheduler stopped.\n";
        }
        else if (choice == 7) {
            std::cout << "[*] Exiting...\n";
            break;
        }
        else {
            std::cout << "[-] Invalid choice. Try again.\n";
        }
    }


    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
