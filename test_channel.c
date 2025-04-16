/**
 * test_channel.c - Test program for the channel.c implementation
 * 
 * This program tests the functionality of the channel implementation
 * by simulating different scenarios such as normal operation,
 * handling multiple connections, and error conditions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>

// Mock function to run the channel program
void run_channel(int port, int slot_time) {
    char command[256];
    sprintf(command, "start cmd /k channel.exe %d %d", port, slot_time);
    system(command);
    // Give the channel time to start
    Sleep(1000);
}

// Function to check if a socket is valid
int is_socket_valid(SOCKET sock) {
    if (sock == INVALID_SOCKET) {
        printf("Socket error: %d\n", WSAGetLastError());
        return 0;
    }
    return 1;
}

// Test basic connection to channel
int test_basic_connection(int port) {
    SOCKET sock;
    struct sockaddr_in server_addr;

    printf("Testing basic connection to channel on port %d...\n", port);
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (!is_socket_valid(sock)) return 0;
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return 0;
    }
    
    printf("Basic connection test PASSED\n");
    closesocket(sock);
    return 1;
}

// Test sending a single packet through the channel
int test_single_packet(int port) {
    SOCKET sock;
    struct sockaddr_in server_addr;
    char header[18] = {0};
    char payload[100] = "This is a test packet";
    char *packet;
    char received[118] = {0};  // Header + payload size
    int packet_size = 18 + strlen(payload);
    
    printf("Testing sending a single packet...\n");
    
    // Create a packet with header
    packet = (char*)malloc(packet_size);
    if (!packet) {
        printf("Memory allocation failed\n");
        return 0;
    }
    
    // Build Ethernet header
    // Destination MAC (6 bytes)
    memcpy(packet, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    // Source MAC (6 bytes)
    memcpy(packet + 6, "\x11\x22\x33\x44\x55\x66", 6);
    // Ethertype (2 bytes)
    packet[12] = 0x08;
    packet[13] = 0x00;
    // Frame size (4 bytes, big-endian)
    packet[14] = 0;
    packet[15] = 0;
    packet[16] = (100 >> 8) & 0xFF;
    packet[17] = 100 & 0xFF;
    
    // Add payload
    memcpy(packet + 18, payload, strlen(payload));
    
    // Connect to channel
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (!is_socket_valid(sock)) {
        free(packet);
        return 0;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(sock);
        free(packet);
        return 0;
    }
    
    // Set receive timeout
    int timeout = 2000; // 2 seconds
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    
    // Send packet
    if (send(sock, packet, packet_size, 0) == SOCKET_ERROR) {
        printf("Send failed: %d\n", WSAGetLastError());
        closesocket(sock);
        free(packet);
        return 0;
    }
    
    // Receive echoed packet
    int bytes_received = recv(sock, received, sizeof(received), 0);
    if (bytes_received == SOCKET_ERROR) {
        printf("Receive failed: %d\n", WSAGetLastError());
        closesocket(sock);
        free(packet);
        return 0;
    }
    
    // Verify the payload data matches what we sent
    if (memcmp(received, payload, strlen(payload)) != 0) {
        printf("Packet data mismatch\n");
        closesocket(sock);
        free(packet);
        return 0;
    }
    
    printf("Single packet test PASSED\n");
    closesocket(sock);
    free(packet);
    return 1;
}

// Test for collision handling (two clients sending simultaneously)
void test_collision_detection(int port) {
    SOCKET sock1, sock2;
    struct sockaddr_in server_addr;
    char header[18] = {0};
    char payload[100] = "This is a test packet";
    char *packet;
    char received[118] = {0};  // Header + payload size
    int packet_size = 18 + strlen(payload);
    
    printf("Testing collision detection...\n");
    
    // Create a packet with header
    packet = (char*)malloc(packet_size);
    if (!packet) {
        printf("Memory allocation failed\n");
        return;
    }
    
    // Build Ethernet header
    // Destination MAC (6 bytes)
    memcpy(packet, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    // Source MAC (6 bytes)
    memcpy(packet + 6, "\x11\x22\x33\x44\x55\x66", 6);
    // Ethertype (2 bytes)
    packet[12] = 0x08;
    packet[13] = 0x00;
    // Frame size (4 bytes, big-endian)
    packet[14] = 0;
    packet[15] = 0;
    packet[16] = (100 >> 8) & 0xFF;
    packet[17] = 100 & 0xFF;
    
    // Add payload
    memcpy(packet + 18, payload, strlen(payload));
    
    // Initialize two sockets
    sock1 = socket(AF_INET, SOCK_STREAM, 0);
    sock2 = socket(AF_INET, SOCK_STREAM, 0);
    
    if (!is_socket_valid(sock1) || !is_socket_valid(sock2)) {
        if (sock1 != INVALID_SOCKET) closesocket(sock1);
        if (sock2 != INVALID_SOCKET) closesocket(sock2);
        free(packet);
        return;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);
    
    // Connect both sockets
    if (connect(sock1, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR ||
        connect(sock2, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed\n");
        closesocket(sock1);
        closesocket(sock2);
        free(packet);
        return;
    }
    
    // Set receive timeout
    int timeout = 2000; // 2 seconds
    setsockopt(sock1, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock2, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    
    // Send packet from both clients (almost) simultaneously
    if (send(sock1, packet, packet_size, 0) == SOCKET_ERROR) {
        printf("Send from client 1 failed: %d\n", WSAGetLastError());
    }
    
    if (send(sock2, packet, packet_size, 0) == SOCKET_ERROR) {
        printf("Send from client 2 failed: %d\n", WSAGetLastError());
    }
    
    // Receive responses
    int bytes_received1 = recv(sock1, received, sizeof(received), 0);
    if (bytes_received1 == SOCKET_ERROR) {
        printf("Receive on client 1 failed: %d\n", WSAGetLastError());
    } else {
        // Check if we received the noise signal
        if (strncmp(received, "!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!",
                    strlen("!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!")) == 0) {
            printf("Collision detected correctly on client 1\n");
        } else {
            printf("Unexpected data on client 1: %.*s\n", bytes_received1, received);
        }
    }
    
    memset(received, 0, sizeof(received));
    
    int bytes_received2 = recv(sock2, received, sizeof(received), 0);
    if (bytes_received2 == SOCKET_ERROR) {
        printf("Receive on client 2 failed: %d\n", WSAGetLastError());
    } else {
        // Check if we received the noise signal
        if (strncmp(received, "!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!",
                    strlen("!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!")) == 0) {
            printf("Collision detected correctly on client 2\n");
        } else {
            printf("Unexpected data on client 2: %.*s\n", bytes_received2, received);
        }
    }
    
    printf("Collision detection test complete\n");
    closesocket(sock1);
    closesocket(sock2);
    free(packet);
}

// Test connection limit
void test_connection_limit(int port) {
    SOCKET sockets[12]; // Testing with MAX_SERVERS + 2
    struct sockaddr_in server_addr;
    int i, success_count = 0;
    
    printf("Testing connection limit...\n");
    
    // Create and connect multiple sockets
    for (i = 0; i < 12; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (sockets[i] == INVALID_SOCKET) {
            printf("Socket creation failed: %d\n", WSAGetLastError());
            continue;
        }
        
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        server_addr.sin_port = htons(port);
        
        if (connect(sockets[i], (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            printf("Connection %d failed: %d\n", i, WSAGetLastError());
            closesocket(sockets[i]);
            sockets[i] = INVALID_SOCKET;
        } else {
            success_count++;
            printf("Connection %d successful\n", i);
        }
    }
    
    printf("Successfully established %d connections\n", success_count);
    
    // Close all sockets
    for (i = 0; i < 12; i++) {
        if (sockets[i] != INVALID_SOCKET) {
            closesocket(sockets[i]);
        }
    }
}

// Main test function
int main() {
    int port = 6342;
    int slot_time = 100;
    WSADATA wsaData;
    
    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }
    
    // Start the channel program
    run_channel(port, slot_time);
    printf("Channel program started\n");
    
    // Run tests
    if (!test_basic_connection(port)) {
        printf("Basic connection test FAILED\n");
        WSACleanup();
        return 1;
    }
    
    if (!test_single_packet(port)) {
        printf("Single packet test FAILED\n");
        WSACleanup();
        return 1;
    }
    
    test_collision_detection(port);
    
    test_connection_limit(port);
    
    printf("\nAll tests completed. Press Ctrl+Z in the channel window to terminate.\n");
    
    // Clean up
    WSACleanup();
    return 0;
}
