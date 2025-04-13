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



#pragma comment(lib, "Ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 1024
#define EXPECTED_CHUNKS 4

std::atomic<int> clientCounter(1);
std::mutex fileMapMutex;
sockaddr_in clientAddr;
int clientAddrLen = sizeof(clientAddr);
int expectedChunks = EXPECTED_CHUNKS;

struct FileTracker {
    int totalChunks = 0;
    std::atomic<int> receivedChunks{ 0 };
};

std::map<std::string, FileTracker> fileChunkMap; 


std::map<std::string, std::vector<std::string>> validTokens = {
    {"mySecret123", {"html.txt"}},
    {"myNew123", {"Report.pdf"}},
    {"adminToken", {"*"}} 
};



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
                std::cerr << "[-] Failed to restore chunk: " << i << "\n";
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
            std::cerr << "[-] Missing replica chunk: " << replicaPath << "\n";
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
            std::cerr << "[-] Failed to restore merged file: " << e.what() << "\n";
            allRestored = false;
        }
    }
    else {
        std::cout << "[!] No  merged file found for: " << filename << "\n";
    
    }

    return allRestored;
}

void mergeChunksIfComplete(const std::string& filename) {
    std::lock_guard<std::mutex> lock(fileMapMutex);

    FileTracker& tracker = fileChunkMap[filename];
    if (tracker.receivedChunks != tracker.totalChunks)
        return;

    std::string outputPath = "merged/" + filename;
    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "[-] Failed to open output file for merging: " << outputPath << "\n";
        return;
    }

    for (int i = 1; i <= tracker.totalChunks; ++i) {
        std::string chunkPath = "temp_chunks/" + filename + "_chunk" + std::to_string(i) + ".txt";
        std::ifstream inFile(chunkPath, std::ios::binary);
        if (!inFile.is_open()) {
            std::cerr << "[-] Missing chunk: " << chunkPath << "\n";
            return;
        }

        outFile << inFile.rdbuf();
        inFile.close();
    }

    std::cout << "[+] All chunks received. File merged: " << outputPath << "\n";
    outFile.close();

   
    try {
        std::filesystem::create_directories("replica_merged");
        std::filesystem::copy_file(
            outputPath,
            "replica_merged/" + filename,
            std::filesystem::copy_options::overwrite_existing
        );
        std::cout << "[+] Replica of merged file stored at: replica_merged/" << filename << "\n";
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[-] Failed to store replica of merged file: " << e.what() << "\n";
    }

   
    for (int i = 1; i <= tracker.totalChunks; ++i) {
        std::filesystem::remove("temp_chunks/" + filename + "_chunk" + std::to_string(i) + ".txt");
    }
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

void handleClient(SOCKET clientSocket) 
{
    char buffer[BUFFER_SIZE];

  
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

   
    size_t newlinePos = metadataLine.find('\n');
    std::string metadata = metadataLine.substr(0, newlinePos);
    std::string leftover = metadataLine.substr(newlinePos + 1); 

    std::cout << "[*] Metadata received: " << metadata << "\n";

  
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
    token.erase(token.find_last_not_of(" \r\n\t") + 1); 

    std::cout << "[*] Extracted Token: " << token << "\n";

    if (!isAuthorized(token, filename)) {
        std::cerr << "[-] Unauthorized access attempt for file: " << filename << "\n";
        closesocket(clientSocket);
        return;
    }
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

    // Step 5: Handle any leftover data after metadata
    if (!leftover.empty()) {
        chunkFile.write(leftover.c_str(), leftover.size());
        replicaFile.write(leftover.c_str(), leftover.size());
    }

    // Step 6: Receive the rest of the file chunk
    while ((bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        chunkFile.write(buffer, bytesReceived);
        replicaFile.write(buffer, bytesReceived);
    }

    chunkFile.close();
    replicaFile.close();

    if (bytesReceived < 0) {
        std::cerr << "[-] Error while receiving file chunk data.\n";
    }
    else {
        std::cout << "[+] Chunk " << chunkId << " received for file: " << filename << "\n";

        {
            std::lock_guard<std::mutex> lock(fileMapMutex);
            if (fileChunkMap.find(filename) == fileChunkMap.end()) {
                fileChunkMap[filename].totalChunks = totalChunks;
            }
            fileChunkMap[filename].receivedChunks++;
        }

        mergeChunksIfComplete(filename);
    }

    closesocket(clientSocket);
}




int main() {
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
        std::cout << "3. Exit\n";

        std::cout << "Enter choice: ";

        int choice;
        std::cin >> choice;
        std::cin.ignore(); // flush newline

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