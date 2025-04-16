#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <stdint.h>

#define MAX_SERVERS 50
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

void reset_all_send_flags(Output *head)
{
    Sleep(1000);

    Output *current = head->next; // Skip head node
    while (current != NULL)
    {
        current->send_in_slot = 0; // Reset flag
        if (current->data_buffer)
        {
            free(current->data_buffer);
            current->data_buffer = NULL;
        }
        current->data_size = 0;
        current = current->next;
    }
}

int count_active(Output *head)
{
    int count = 0;
    Output *current = head->next; // Include the head node in the count
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
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <chan_port> <chan_port>\n", argv[0]);
        return 1;
    }
    Output *head = (Output *)malloc(sizeof(Output));
    if (!head)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    memset(head, 0, sizeof(Output));
    head->next = NULL;
    Output *current = head;
    Input *c1 = (Input *)malloc(sizeof(Input));
    if (!c1)
    {
        fprintf(stderr, "Memory allocation failed\n");
        free_list(head);
        return 1;
    }
    memset(c1, 0, sizeof(Input));
    c1->chan_port = atoi(argv[1]);
    c1->slot_time = atoi(argv[2]);
    // Initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR)
    {
        fprintf(stderr, "Error at WSAStartup(): %d\n", iResult);
        free(c1);
        free_list(head);
        return 1;
    }

    // Create a TCP socket
    SOCKET tcp_s = socket(AF_INET, SOCK_STREAM, 0); // listening to connections
    if (tcp_s == INVALID_SOCKET)
    {
        fprintf(stderr, "Error creating socket: %d\n", WSAGetLastError());
        WSACleanup();
        free(c1);
        free_list(head);
        return 1;
    }

    struct sockaddr_in my_addr;
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(c1->chan_port);

    struct sockaddr_in server_addr;
    int server_addr_len = sizeof(server_addr);

    char *data_buffer;
    int bytes_received;

    // Bind the socket to the port
    if (bind(tcp_s, (SOCKADDR *)&my_addr, sizeof(my_addr)) == SOCKET_ERROR)
    {
        fprintf(stderr, "Bind failed: %d\n", WSAGetLastError());
        closesocket(tcp_s);
        WSACleanup();
        free(c1);
        free_list(head);
        return 1;
    }

    if (listen(tcp_s, MAX_SERVERS) == SOCKET_ERROR)
    {
        fprintf(stderr, "Listen failed: %d\n", WSAGetLastError());
        closesocket(tcp_s);
        WSACleanup();
        free(c1);
        free_list(head);
        return 1;
    }

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
        timeout.tv_sec = c1->slot_time / 1000;           // Convert milliseconds to seconds
        timeout.tv_usec = (c1->slot_time % 1000) * 1000; // Remaining milliseconds to microseconds

        int max_fd = 0;
        for (u_int i = 0; i < master_set.fd_count; i++)
        {
            if ((int)master_set.fd_array[i] > max_fd)
                max_fd = (int)master_set.fd_array[i];
        }
        int ready = select(0, &read_fds, NULL, NULL, &timeout); // still Windows uses 0

        if (ready == SOCKET_ERROR)
        {
            printf("Select failed: %d\n", WSAGetLastError());
            break;
        }
        else if (ready == 0)
        {
            // Timeout occurred
            continue;
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
                        if (!new_output)
                        {
                            fprintf(stderr, "Memory allocation failed\n");
                            continue; // Skip this connection but keep the server running
                        }
                        new_output->sender_address = _strdup(inet_ntoa(server_addr.sin_addr));
                        if (!new_output->sender_address)
                        {
                            fprintf(stderr, "Memory allocation failed\n");
                            free(new_output);
                            continue;
                        }
                        new_output->socket = new_server;
                        new_output->avg_bw = 0;
                        new_output->frame_size = 0;
                        new_output->num_packets = 0;
                        new_output->total_collisions = 0;
                        new_output->send_in_slot = 0;
                        new_output->data_size = 0;
                        new_output->port_num = ntohs(server_addr.sin_port);
                        new_output->data_buffer = NULL;
                        new_output->next = NULL;
                        new_output->start_time = GetTickCount();
                        new_output->end_time = new_output->start_time;
                        current->next = new_output;
                        current = new_output;
                    }
                }
                else // listen to messages
                {
                    if (FD_ISSET(socket, &read_fds))
                    {
                        FD_CLR(socket, &read_fds);
                        memset(buffer, 0, HEADER_SIZE + 1);
                        int header_received = recv(socket, buffer, HEADER_SIZE, 0);
                        buffer[HEADER_SIZE] = '\0';

                        if (header_received > 0)
                        {
                            // Find the corresponding server
                            Output *ptr = head->next;
                            while (ptr)
                            {
                                if (ptr->socket == socket)
                                {
                                    // Extract frame size from header
                                    ptr->frame_size = ((uint8_t)buffer[14] << 24) |
                                                      ((uint8_t)buffer[15] << 16) |
                                                      ((uint8_t)buffer[16] << 8) |
                                                      ((uint8_t)buffer[17]);
                                    ptr->num_packets++;
                                    ptr->send_in_slot = 1; // Mark this server as active in this slot

                                    // Read the data if frame size is valid
                                    if (ptr->frame_size > 0)
                                    {
                                        // Allocate buffer for this server's data
                                        ptr->data_buffer = (char *)malloc(ptr->frame_size + 1);
                                        if (!ptr->data_buffer)
                                        {
                                            fprintf(stderr, "Memory allocation failed\n");
                                            break;
                                        }

                                        // Receive the data portion
                                        ptr->data_size = recv(socket, ptr->data_buffer, ptr->frame_size, 0);
                                        if (ptr->data_size <= 0)
                                        {
                                            free(ptr->data_buffer);
                                            ptr->data_buffer = NULL;
                                            ptr->data_size = 0;
                                            break;
                                        }
                                        ptr->data_buffer[ptr->frame_size] = '\0'; // Null-terminate the data
                                    }
                                    break;
                                }
                                ptr = ptr->next;
                            }
                        }
                        else
                        {
                            // server disconnected
                            Output *prev = head;
                            Output *ptr = head->next;
                            while (ptr)
                            {
                                if (ptr->socket == socket)
                                {
                                    break;
                                }
                                prev = ptr;
                                ptr = ptr->next;
                            }
                            if (ptr)
                            {
                                ptr->end_time = GetTickCount();
                                double elapsed_time = (ptr->end_time - ptr->start_time) / 1000.0; // in seconds

                                // Avoid division by zero
                                if (elapsed_time > 0)
                                {
                                    ptr->avg_bw = (double)(ptr->frame_size * ptr->num_packets * 8) / (elapsed_time * 1000000); // in Mbps
                                }
                                else
                                {
                                    ptr->avg_bw = 0;
                                }
                                fprintf(stderr, "\nFrom %s port %d: %d frames, %d collisions, average bandwidth: %.3f Mbps\n",
                                        ptr->sender_address, ptr->port_num, ptr->num_packets, ptr->total_collisions, ptr->avg_bw);
                                prev->next = ptr->next;

                                if (current == ptr)
                                {
                                    current = prev;
                                }

                                free(ptr->sender_address);
                                if (ptr->data_buffer)
                                {
                                    free(ptr->data_buffer);
                                }
                                free(ptr);
                                closesocket(socket);
                                FD_CLR(socket, &master_set);
                            }
                        }
                    }
                }
            }
        }
        // After processing all sockets, check for collisions
        int active_count = count_active(head);
        printf("active count: %d\n", active_count); // DEBUG

        // Handle collisions or successful transmission
        if (active_count > 1) // Collision detected
        {
            // Prepare noise signal
            const char *noise = "!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!";
            int noise_len = (int)strlen(noise);

            // Update collision counters for all active servers
            Output *ptr = head->next;
            while (ptr)
            {
                if (ptr->send_in_slot == 1)
                {
                    ptr->total_collisions++;
                }
                ptr = ptr->next;
            }

            // Send noise signal to all servers
            for (u_int j = 0; j < master_set.fd_count; j++)
            {
                SOCKET out_socket = master_set.fd_array[j];
                if (out_socket != tcp_s) // Don't send to the listening socket
                {
                    if (send(out_socket, noise, noise_len, 0) == SOCKET_ERROR)
                    {
                        fprintf(stderr, "Error sending noise: %d\n", WSAGetLastError());
                    }
                }
            }
            reset_all_send_flags(head);
        }
        else if (active_count == 1) // Exactly one sender, no collision
        {
            // Find the active server
            Output *active_ptr = head->next;
            while (active_ptr)
            {
                if (active_ptr->send_in_slot == 1)
                {
                    // Send data from this server to all servers
                    for (u_int j = 0; j < master_set.fd_count; j++)
                    {
                        SOCKET out_socket = master_set.fd_array[j];
                        if (out_socket != tcp_s) // Don't send to the listening socket
                        {
                            if (send(out_socket, active_ptr->data_buffer, active_ptr->data_size, 0) == SOCKET_ERROR)
                            {
                                fprintf(stderr, "Error sending data: %d\n", WSAGetLastError());
                            }
                        }
                    }
                    break;
                }
                active_ptr = active_ptr->next;
            }
            reset_all_send_flags(head);
        }

        // If no active servers (active_count == 0), do nothing
    }
    closesocket(tcp_s);
    WSACleanup();
    free(c1);
    free_list(head);
    return 0;
}