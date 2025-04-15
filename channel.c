#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <stdint.h>

#define MAX_SERVERS 10
#define MSG_SIZE 1024
#define HEADER_SIZE 18

typedef struct Input
{
    int chan_port;
    int slot_time;
} Input;

typedef struct Output
{
    SOCKET socket;
    char *sender_address;
    int port_num;
    int num_packets;
    int total_collisions;
    int avg_bw;
    DWORD start_time;
    DWORD end_time;
    int send_in_slot;
    struct Output *next;
} Output;

void free_list(Output *head)
{
    Output *current = head;
    while (current != NULL)
    {
        Output *temp = current;
        current = current->next;
        free(temp->sender_address);
        free(temp);
    }
}

int count_active(Output *head)
{
    int count = 0;
    Output *current = head->next; // Skip the head node
    while (current != NULL)
    {
        if (current->send_in_slot == 1) // Check if a packet was sent in the slot
        {
            count++;
        }
        current = current->next;
    }
    return count;
}

int main(int argc, char *argv[])
{
    Output *head = (Output *)malloc(sizeof(Output));
    head->next = NULL;
    Output *current = head;
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <chan_ip> <chan_port>\n", argv[0]);
        return 1;
    }
    Input *c1 = (Input *)malloc(sizeof(Input));
    c1->chan_port = atoi(argv[1]);
    c1->slot_time = atoi(argv[2]);
    // Initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR)
        printf("Error at WSAStartup()\n");

    // Create a TCP socket
    SOCKET tcp_s = socket(AF_INET, SOCK_STREAM, 0); // listening to connections
    struct sockaddr_in my_addr;
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(c1->chan_port);

    struct sockaddr_in server_addr;
    int server_addr_len = sizeof(server_addr);

    // Bind the socket to the port
    bind(tcp_s, (SOCKADDR *)&my_addr, sizeof(my_addr));
    listen(tcp_s, MAX_SERVERS);

    fd_set master_set, read_fds;
    FD_ZERO(&master_set);
    FD_SET(tcp_s, &master_set); // Add the listening socket to the master set

    char buffer[HEADER_SIZE + 1]; // Buffer for incoming messages

    // Connection was recieved
    while (1)
    {
        read_fds = master_set;
        // Set the timeout for select
        struct timeval timeout;
        timeout.tv_sec = c1->slot_time;
        timeout.tv_usec = 0;

        int ready = select(0, &read_fds, NULL, NULL, &timeout); // waits until a socket is ready
        if (ready == SOCKET_ERROR)
        {
            printf("Select failed: %d\n", WSAGetLastError());
            break;
        }
        for (u_int i = 0; i < master_set.fd_count; i++)
        {
            SOCKET socket = read_fds.fd_array[i];
            if (FD_ISSET(socket, &read_fds)) // check if socket is ready
            {
                if (socket == tcp_s) // New connection on the listening socket

                {
                    SOCKET new_server = accept(tcp_s, (SOCKADDR *)&server_addr, &server_addr_len);

                    if (new_server != INVALID_SOCKET)
                    {
                        FD_SET(new_server, &master_set); // Add the new socket to the master set
                        Output *new_output = (Output *)malloc(sizeof(Output));
                        new_output->socket = new_server;
                        new_output->sender_address = _strdup(inet_ntoa(server_addr.sin_addr));
                        new_output->port_num = ntohs(server_addr.sin_port);
                        new_output->num_packets = 0;
                        new_output->total_collisions = 0;
                        new_output->send_in_slot = 0; // Initialize send_in_slot to 0
                        new_output->avg_bw = 0;
                        new_output->next = NULL;
                        new_output->start_time = GetTickCount();
                        new_output->end_time = 0;
                        current->next = new_output;
                        current = new_output;
                    }
                }
                else // listen to messages
                {
                    int header_received = recv(socket, buffer, HEADER_SIZE, 0);
                    if (header_received > 0)
                    {
                        uint32_t frame_size = ((uint8_t)buffer[14] << 24) |
                                              ((uint8_t)buffer[15] << 16) |
                                              ((uint8_t)buffer[16] << 8) |
                                              (uint8_t)buffer[17];
                        char *data_buffer = malloc(frame_size + 1);
                        if (!data_buffer)
                        {
                            fprintf(stderr, "Memory allocation failed\n");
                            continue;
                        }
                        int bytes_received = recv(socket, data_buffer, frame_size, 0);
                        if (bytes_received <= 0)
                        {
                            // Handle broken connection
                            continue;
                        }

                        data_buffer[bytes_received] = '\0';
                        Output *ptr = head->next;
                        while (ptr)
                        {
                            if (ptr->socket == socket)
                            {
                                ptr->num_packets++;    // One frame = one packet in your case
                                ptr->send_in_slot = 1; // Set the flag to indicate that a packet was sent in the slot
                                break;
                            }
                            ptr = ptr->next;
                        }
                        int collisions = count_active(head->next); // Count active connections
                        if (collisions > 1)
                        {
                            printf("NOISE\n"); // DEBUG
                            strcpy(data_buffer, "!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!\n");
                            Output *ptr = head->next;
                            while (ptr)
                            {
                                if (ptr->socket == socket)
                                {
                                    break;
                                }
                                ptr = ptr->next;
                            }
                            ptr->total_collisions++;
                        }
                        for (u_int j = 0; j < master_set.fd_count; j++)
                        {
                            SOCKET out_socket = master_set.fd_array[j];
                            if (out_socket != tcp_s)
                            {
                                send(out_socket, data_buffer, bytes_received, 0);
                            }
                        }
                        free(data_buffer); // Free the data buffer after processing
                    }
                    else
                    {
                        // Client disconnected
                        Output *ptr = head->next;
                        while (ptr)
                        {
                            if (ptr->socket == socket)
                            {
                                break;
                            }
                            ptr = ptr->next;
                        }
                        if (ptr)
                        {
                            ptr->end_time = GetTickCount();
                            ptr->avg_bw = (ptr->num_packets * 8) / ((ptr->end_time - ptr->start_time) * 1000); // in bps
                            fprintf(stderr, "\nFrom %s port %d: %d frames, %d collisions, average bandwidth: %.3f Mbps\n",
                                    ptr->sender_address, ptr->port_num, ptr->num_packets, ptr->total_collisions, ptr->avg_bw);
                        }
                        ptr->num_packets = 0;
                        ptr->total_collisions = 0;
                        ptr->send_in_slot = 0; // Reset send_in_slot for the next connection
                        closesocket(socket);
                        FD_CLR(socket, &master_set);
                    }
                }
            }
        }
    }
    closesocket(tcp_s);
    WSACleanup();
    free(c1);
    free_list(head);
    return 0;
}
