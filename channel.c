#include "header.h"

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <chan_port> <chan_port>\n", argv[0]);
        return 1;
    }
    OutputChannel *head = (OutputChannel *)malloc(sizeof(OutputChannel));
    if (!head)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    memset(head, 0, sizeof(OutputChannel));
    head->next = NULL;
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
                        OutputChannel *new_OutputChannel = (OutputChannel *)malloc(sizeof(OutputChannel));
                        if (!new_OutputChannel)
                        {
                            fprintf(stderr, "Memory allocation failed\n");
                            continue; // Skip this connection but keep the server running
                        }
                        new_OutputChannel->sender_address = _strdup(inet_ntoa(server_addr.sin_addr));
                        if (!new_OutputChannel->sender_address)
                        {
                            fprintf(stderr, "Memory allocation failed\n");
                            free(new_OutputChannel);
                            continue;
                        }
                        new_OutputChannel->socket = new_server;
                        new_OutputChannel->avg_bw = 0;
                        new_OutputChannel->frame_size = 0;
                        new_OutputChannel->num_packets = 0;
                        new_OutputChannel->total_collisions = 0;
                        new_OutputChannel->data_size = 0;
                        new_OutputChannel->port_num = ntohs(server_addr.sin_port);
                        new_OutputChannel->data_buffer = NULL;
                        new_OutputChannel->next = NULL;
                        new_OutputChannel->start_time = GetTickCount();
                        new_OutputChannel->end_time = new_OutputChannel->start_time;
                    }
                }
                else // listen to messages
                {
                    if (FD_ISSET(socket, &read_fds))
                    {
                        FD_CLR(socket, &read_fds);
                        memset(buffer, 0, HEADER_SIZE + 1);
                        int header_received = recv(socket, buffer, HEADER_SIZE, 0);
                        printf("\n recv_msg_header: %s len of msg: %d\n", buffer, strlen(buffer)); // DEBUG
                        buffer[HEADER_SIZE] = '\0';

                        if (header_received > 0)
                        {
                            // Find the corresponding server
                            OutputChannel *ptr = head->next;
                            while (ptr)
                            {
                                if (ptr->socket == socket)
                                {
                                    // Extract frame size from header
                                    ptr->frame_size = ((uint8_t)buffer[14] << 24) |
                                                      ((uint8_t)buffer[15] << 16) |
                                                      ((uint8_t)buffer[16] << 8) |
                                                      ((uint8_t)buffer[17]);
                                    printf("\nframe size: %d\n", ptr->frame_size); // DEBUG
                                    ptr->num_packets++;

                                    // Read the data if frame size is valid
                                    if (ptr->frame_size > 0)
                                    {
                                        // Allocate buffer for this server's data
                                        ptr->data_buffer = (char *)malloc(ptr->frame_size + 1);
                                        memset(ptr->data_buffer, 0, ptr->frame_size + 1); // Initialize buffer
                                        if (!ptr->data_buffer)
                                        {
                                            fprintf(stderr, "Memory allocation failed\n");
                                            break;
                                        }

                                        // Receive the data portion
                                        ptr->data_size = recv(socket, ptr->data_buffer, ptr->frame_size, 0);
                                        printf("\n recv_msg: %s len of msg: %d, data_size: %d\n", ptr->data_buffer, strlen(ptr->data_buffer), ptr->data_size); // DEBUG
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
                            OutputChannel *prev = head;
                            OutputChannel *ptr = head->next;
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
        printf("active count: %d\n", master_set.fd_count); // DEBUG

        // Handle collisions or successful transmission
        if (master_set.fd_count > 2) // Collision detected
        {
            // Prepare noise signal
            const char *noise = "!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!";
            int noise_len = (int)strlen(noise);
            char *padded_noise = NULL;

            // Update collision counters for all active servers
            OutputChannel *ptr = head->next;
            while (ptr)
            {

                ptr->total_collisions++;
                padded_noise = (char *)malloc(ptr->frame_size);
                memset(padded_noise, 0, ptr->frame_size);
                memcpy(padded_noise, noise, noise_len);
                printf("padded noise: %s\n", padded_noise);
                SOCKET out_socket = ptr->socket;
                if (out_socket != tcp_s)
                {
                    if (send(out_socket, padded_noise, ptr->frame_size, 0) == SOCKET_ERROR)
                    {
                        fprintf(stderr, "Error sending noise: %d\n", WSAGetLastError());
                    }
                }

                ptr = ptr->next;
            }
        }
        else if (master_set.fd_count == 2) // Exactly one sender, no collision
        {
            // Find the active server
            OutputChannel *active_ptr = head->next;
            while (active_ptr)
            {
                SOCKET out_socket = active_ptr->socket;
                if (out_socket != tcp_s) // Don't send to the listening socket
                {
                    printf("\n sent msg : %s len of msg: %d\n", active_ptr->data_buffer, active_ptr->data_size); // DEBUG
                    if (send(out_socket, active_ptr->data_buffer, active_ptr->data_size, 0) == SOCKET_ERROR)
                    {
                        fprintf(stderr, "Error sending data: %d\n", WSAGetLastError());
                    }
                }
                active_ptr = active_ptr->next;
            }
        }

        // If no active servers (active_count == 0), do nothing
    }
    closesocket(tcp_s);
    WSACleanup();
    free(c1);
    free_list(head);
    return 0;
}

void free_list(OutputChannel *head)
{
    OutputChannel *current = head;
    while (current != NULL)
    {
        OutputChannel *temp = current;
        current = current->next;
        free(temp->sender_address);
        free(temp);
    }
}
