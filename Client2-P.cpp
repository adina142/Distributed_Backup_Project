#include <iostream>
#include <fstream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024

void sendChunk(const std::string& filename, const std::string& token, int chunkId, int totalChunks, std::streamoff start, std::streamsize chunkSize) {
    WSADATA wsaData;
    SOCKET clientSocket;
    sockaddr_in serverAddr;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[-] WSAStartup failed.\n";
        return;
    }

    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "[-] Socket creation failed.\n";
        return;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[-] Connection failed.\n";
        closesocket(clientSocket);
        return;
    }

   
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[-] Failed to open file.\n";
        closesocket(clientSocket);
        return;
    }

    file.seekg(start);

    std::string baseFilename = filename.substr(filename.find_last_of("/\\") + 1);
    std::string metadata = baseFilename + "|" + std::to_string(chunkId) + "|" + std::to_string(totalChunks) + "|" + token + "\r\n\r\n";

    if (send(clientSocket, metadata.c_str(), metadata.size(), 0) == SOCKET_ERROR) {
        std::cerr << "[-] Failed to send metadata.\n";
        closesocket(clientSocket);
        return;
    }

   
    char buffer[BUFFER_SIZE];
    std::streamsize sent = 0;
    while (sent < chunkSize && file) {
        std::streamsize toRead = std::min<std::streamsize>(BUFFER_SIZE, chunkSize - sent);
        file.read(buffer, toRead);
        int bytesRead = file.gcount();
        if (bytesRead > 0) {
            send(clientSocket, buffer, bytesRead, 0);
            sent += bytesRead;
        }
        else {
            break;
        }
    }

    std::cout << "[+] Sent chunk " << chunkId << "\n";

    file.close();
    closesocket(clientSocket);
    WSACleanup();
}

int main() {
    const std::string filename = "Report.pdf";  
    const std::string token = "myNew123";
    const int totalChunks = 2;                 

    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[-] Could not open file.\n";
        return 1;
    }

    std::streamoff fileSize = file.tellg();
    file.close();

    std::streamsize chunkSize = fileSize / totalChunks;
    std::streamoff offset = 0;

    for (int i = 1; i <= totalChunks; ++i) {
        std::streamsize thisChunkSize = (i == totalChunks) ? fileSize - offset : chunkSize;
        sendChunk(filename, token, i, totalChunks, offset, thisChunkSize);
        offset += thisChunkSize;
    }

    std::cout << "[*] All chunks sent successfully.\n";
    return 0;
}
