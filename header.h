#ifndef NETWORK_SIM_H
#define NETWORK_SIM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <stdint.h>
#include <windows.h>
#include <conio.h>

// Shared constants
#define HEADER_SIZE 18
#define MAX_SERVERS 50
#define MSG_SIZE 1024

// Input structure for both server and channel
typedef struct Input
{
    // Channel-specific
    int chan_port;
    int slot_time;

    // Server-specific
    char *chan_ip;
    char *file_name;
    int frame_size;
    int seed;
    int timeout;
} Input;

// Output structure for channel
typedef struct OutputChannel
{
    SOCKET socket;
    int frame_size;
    char *sender_address;
    int port_num;
    int num_packets;
    int total_collisions;
    double avg_bw;
    DWORD start_time;
    DWORD end_time;
    int send_in_slot;
    char *data_buffer;
    int data_size;
    struct OutputChannel *next;
} OutputChannel;

// Output structure for server
typedef struct OutputServer
{
    char *file_name;
    int success;
    int file_size;
    int num_of_packets;
    int total_time;
    int max_transmissions;
    double avg_transmissions;
    double avg_bw;
} OutputServer;

// Channel-side functions
void free_list(OutputChannel *head);
void reset_all_send_flags(OutputChannel *head);
DWORD WINAPI monitor_ctrl_z(LPVOID param);

// Server-side functions
void exponential_backoff(int k, int slot_time);
BOOL WINAPI ctrl_handler(DWORD ctrl_type);

#endif // NETWORK_SIM_H
