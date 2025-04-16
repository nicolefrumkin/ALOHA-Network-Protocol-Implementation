/**
 * test_server.c - Test program for the server.c implementation
 * 
 * This program tests the functionality of the server implementation by:
 * 1. Creating a mock channel to verify correct packet transmission
 * 2. Testing the exponential backoff mechanism
 * 3. Testing timeout handling
 * 4. Verifying correct file transmission
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdint.h>

#define HEADER_SIZE 18

// Create a test file of specified size
void create_test_file(const char* filename, int size) {
    FILE* f = fopen(filename, "wb");
    if (!f) {
        printf("Failed to create test file %s\n", filename);
        return;
    }
    
    char* data = (char*)malloc(size);
    if (!data) {
        printf("Memory allocation failed\n");
        fclose(f);
        return;
    }
    
    // Fill with predictable pattern
    for (int i = 0; i < size; i++) {
        data[i] = (char)(i % 256);
    }
    
    fwrite(data, 1, size, f);
    fclose(f);
    free(data);
    printf("Created test file %s (%d bytes)\n", filename, size);
}

// Mock function to run the server program
void run_server(const char* chan_ip, int chan_port, const char* file_name, 
                int frame_size, int slot_time, int seed, int timeout) {
    char command[512];
    sprintf(command, "start cmd /k server.exe %s %d %s %d %d %d %d", 
            chan_ip, chan_port, file_name, frame_size, slot_time, seed, timeout);
    system(command);
}

// Mock channel implementation to test server functionality
DWORD WINAPI mock_channel_thread(LPVOID param) {
    int port = *((int*)param);
    SOCKET listen_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);
    char buffer[4096];
    WSADATA wsaData;
    
    printf("Starting mock channel on port %d...\n", port);
    
    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }
    
    // Create socket
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    
    // Setup address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Bind
    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }
    
    // Listen
    if (listen(listen_sock, 10) == SOCKET_ERROR) {
        printf("Listen failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }
    
    printf("Mock channel waiting for connections...\n");
    
    // Accept a client connection
    client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_sock == INVALID_SOCKET) {
        printf("Accept failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }
    
    printf("Client connected: %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    
    int packet_count = 0;
    int collision_count = 0;
    
    // Simulate channel behavior
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        
        // First receive the header
        int recv_bytes = recv(client_sock, buffer, HEADER_SIZE, 0);
        if (recv_bytes <= 0) {
            if (recv_bytes == 0) {
                printf("Client disconnected\n");
            } else {
                printf("Receive failed: %d\n", WSAGetLastError());
            }
            break;
        }
        
        // Extract frame size from header
        uint32_t frame_size = ((uint8_t)buffer[14] << 24) | 
                              ((uint8_t)buffer[15] << 16) | 
                              ((uint8_t)buffer[16] << 8) | 
                              (uint8_t)buffer[17];
        
        printf("Received header, frame size: %u\n", frame_size);
        
        // Receive the data portion
        recv_bytes = recv(client_sock, buffer, frame_size, 0);
        if (recv_bytes <= 0) {
            if (recv_bytes == 0) {
                printf("Client disconnected during data transfer\n");
            } else {
                printf("Receive data failed: %d\n", WSAGetLastError());
            }
            break;
        }
        
        packet_count++;
        printf("Received packet #%d (%d bytes)\n", packet_count, recv_bytes);
        
        // Decide whether to simulate a collision (every 5th packet)
        if (packet_count % 5 == 0) {
            collision_count++;
            const char* noise = "!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!";
            send(client_sock, noise, strlen(noise), 0);
            printf("Simulated collision for packet #%d\n", packet_count);
        } else {
            // Echo the received data back (successful transmission)
            send(client_sock, buffer, recv_bytes, 0);
        }
        
        // Break after 20 packets for testing purposes
        if (packet_count >= 20) {
            printf("Test complete after %d packets\n", packet_count);
            break;
        }
    }
    
    printf("Mock channel statistics:\n");
    printf("Total packets: %d\n", packet_count);
    printf("Simulated collisions: %d\n", collision_count);
    
    closesocket(client_sock);
    closesocket(listen_sock);
    WSACleanup();
    return 0;
}

// Test exponential backoff by measuring time between transmissions
void test_exponential_backoff() {
    printf("Testing exponential backoff mechanism...\n");
    
    // Create a test file
    create_test_file("test_backoff.dat", 5000);
    
    // Start the mock channel
    int port = 6543;
    HANDLE channel_thread = CreateThread(NULL, 0, mock_channel_thread, &port, 0, NULL);
    if (channel_thread == NULL) {
        printf("Failed to create channel thread\n");
        return;
    }
    
    // Give the channel time to start
    Sleep(1000);
    
    // Run server with a small slot time to observe backoff
    run_server("127.0.0.1", port, "test_backoff.dat", 500, 50, 123, 5);
    
    // Wait for the server to complete (should show in output)
    printf("Server running with exponential backoff test...\n");
    printf("Check the server window for backoff messages\n");
    
    // Wait for the channel thread to finish
    WaitForSingleObject(channel_thread, 30000);  // Wait up to 30 seconds
    CloseHandle(channel_thread);
}

// Test timeout handling
void test_timeout_handling() {
    printf("Testing timeout handling...\n");
    
    // Create a test file
    create_test_file("test_timeout.dat", 8000);
    
    // Start a special version of mock channel that hangs
    SOCKET listen_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);
    int port = 6544;
    char buffer[HEADER_SIZE];
    
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return;
    }
    
    // Create socket
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }
    
    // Setup address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Bind
    if (bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return;
    }
    
    // Listen
    if (listen(listen_sock, 1) == SOCKET_ERROR) {
        printf("Listen failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return;
    }
    
    printf("Mock timeout channel waiting for connections on port %d...\n", port);
    
    // Start the server with a small timeout value
    run_server("127.0.0.1", port, "test_timeout.dat", 500, 50, 123, 2);
    
    // Accept a client connection
    client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_sock == INVALID_SOCKET) {
        printf("Accept failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return;
    }
    
    printf("Client connected for timeout test: %s:%d\n", 
           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    
    // Receive only header, don't respond to test timeout
    int recv_bytes = recv(client_sock, buffer, HEADER_SIZE, 0);
    if (recv_bytes > 0) {
        printf("Received header, now simulating no response to test timeout...\n");
        printf("Check the server window - it should report a timeout\n");
    }
    
    // Wait for server to time out
    Sleep(5000);
    
    closesocket(client_sock);
    closesocket(listen_sock);
    WSACleanup();
}

// Test normal file transmission
void test_file_transmission() {
    printf("Testing complete file transmission...\n");
    
    // Create a test file of reasonable size
    create_test_file("test_file.dat", 50000);
    
    // Start the mock channel
    int port = 6545;
    HANDLE channel_thread = CreateThread(NULL, 0, mock_channel_thread, &port, 0, NULL);
    if (channel_thread == NULL) {
        printf("Failed to create channel thread\n");
        return;
    }
    
    // Give the channel time to start
    Sleep(1000);
    
    // Run server 
    run_server("127.0.0.1", port, "test_file.dat", 1000, 100, 123, 5);
    
    printf("Test file transmission in progress...\n");
    printf("Check the server window for transmission statistics\n");
    
    // Wait for the channel thread to finish
    WaitForSingleObject(channel_thread, INFINITE);
    CloseHandle(channel_thread);
}

// Main test function
int main() {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }
    
    printf("=== Server Test Suite ===\n\n");
    
    // Run tests
    test_exponential_backoff();
    printf("\n");
    
    test_timeout_handling();
    printf("\n");
    
    test_file_transmission();
    printf("\n");
    
    printf("All tests completed\n");
    
    // Clean up
    WSACleanup();
    return 0;
}
