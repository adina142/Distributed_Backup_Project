 #include <iostream>
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9090
#define BUFFER_SIZE 1024

void saveFileFromSocket(SOCKET clientSocket) {
    // Step 1: Receive filename length
    uint32_t nameLen;
    int received = recv(clientSocket, (char*)&nameLen, sizeof(nameLen), 0);
    if (received <= 0) {
        std::cerr << "[-] Failed to receive filename length.\n";
        return;
    }
    nameLen = ntohl(nameLen);

    // Step 2: Receive filename
    std::string filename(nameLen, '\0');
    received = recv(clientSocket, &filename[0], nameLen, 0);
    if (received <= 0) {
        std::cerr << "[-] Failed to receive filename.\n";
        return;
    }

    std::filesystem::create_directories("received_files");
    std::string outputPath = "received_files/" + filename;

    // Step 3: Receive file size
    uint32_t fileSize;
    received = recv(clientSocket, (char*)&fileSize, sizeof(fileSize), 0);
    if (received <= 0) {
        std::cerr << "[-] Failed to receive file size.\n";
        return;
    }
    fileSize = ntohl(fileSize);

    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "[-] Failed to open output file.\n";
        return;
    }

    // Step 4: Receive file content
    char buffer[BUFFER_SIZE];
    uint32_t totalReceived = 0;
    while (totalReceived < fileSize) {
        int remaining = fileSize - totalReceived;
        int bytesToReceive = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
        int bytesReceived = recv(clientSocket, buffer, bytesToReceive, 0);
        if (bytesReceived <= 0) break;

        outFile.write(buffer, bytesReceived);
        totalReceived += bytesReceived;
    }

    outFile.close();
    std::cout << "[+] File received and saved as: " << outputPath << "\n";
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 5);
    std::cout << "[*] server1Replica listening on port " << PORT << "...\n";

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "[-] Failed to accept connection.\n";
            continue;
        }
        std::cout << "[*] Connection from main server accepted.\n";
        saveFileFromSocket(clientSocket);
        closesocket(clientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
