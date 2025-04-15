#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#define MSG_SIZE 1024

typedef struct Input {
    int chan_port;
    int slot_time;
} Input;

typedef struct Output {
    SOCKET socket;
    char *sender_address;
    int port_num;
    int num_packets;
    int total_collisions;
    int avg_bw;
    DWORD start_time;
    DWORD end_time;
    int sent_in_slot;
    struct Output *next;
} Output;

void free_list(Output *head) {
    Output *current = head;
    while (current != NULL) {
        Output *temp = current;
        current = current->next;
        free(temp->sender_address);
        free(temp);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <chan_port> <slot_time_ms>\n", argv[0]);
        return 1;
    }

    Input *c1 = (Input *)malloc(sizeof(Input));
    c1->chan_port = atoi(argv[1]);
    c1->slot_time = atoi(argv[2]);

    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    SOCKET tcp_s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in my_addr, server_addr;
    int server_addr_len = sizeof(server_addr);

    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(c1->chan_port);

    if (bind(tcp_s, (SOCKADDR *)&my_addr, sizeof(my_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    listen(tcp_s, SOMAXCONN);

    fd_set master_set, read_fds;
    FD_ZERO(&master_set);
    FD_SET(tcp_s, &master_set);

    char buffer[MSG_SIZE], temp_buffer[MSG_SIZE];
    Output *head = (Output *)malloc(sizeof(Output));
    head->next = NULL;
    Output *current = head;

    while (1) {
        read_fds = master_set;

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = c1->slot_time * 1000;

        int ready = select(0, &read_fds, NULL, NULL, &timeout);
        if (ready == SOCKET_ERROR) {
            fprintf(stderr, "Select failed: %d\n", WSAGetLastError());
            break;
        }

        // Reset sent_in_slot for all clients
        Output *ptr = head->next;
        while (ptr) {
            ptr->sent_in_slot = 0;
            ptr = ptr->next;
        }

        int sender_count = 0;
        int message_len = 0;

        for (u_int i = 0; i < master_set.fd_count; i++) {
            SOCKET socket = read_fds.fd_array[i];

            if (FD_ISSET(socket, &read_fds)) {
                if (socket == tcp_s) {
                    SOCKET new_server = accept(tcp_s, (SOCKADDR *)&server_addr, &server_addr_len);
                    if (new_server != INVALID_SOCKET) {
                        FD_SET(new_server, &master_set);
                        Output *new_output = (Output *)malloc(sizeof(Output));
                        new_output->socket = new_server;
                        new_output->sender_address = _strdup(inet_ntoa(server_addr.sin_addr));
                        new_output->port_num = ntohs(server_addr.sin_port);
                        new_output->num_packets = 0;
                        new_output->total_collisions = 0;
                        new_output->avg_bw = 0;
                        new_output->start_time = GetTickCount();
                        new_output->end_time = 0;
                        new_output->sent_in_slot = 0;
                        new_output->next = NULL;
                        current->next = new_output;
                        current = new_output;
                    }
                } else {
                    int bytes_received = recv(socket, temp_buffer, MSG_SIZE, 0);
                    if (bytes_received <= 0) {
                        // Client disconnected
                        ptr = head->next;
                        while (ptr) {
                            if (ptr->socket == socket) break;
                            ptr = ptr->next;
                        }
                        if (ptr) {
                            ptr->end_time = GetTickCount();
                            ptr->avg_bw = (ptr->num_packets * 8) / ((ptr->end_time - ptr->start_time) * 1000);
                            fprintf(stderr, "\nFrom %s port %d: %d frames, %d collisions, average bandwidth: %d Mbps\n",
                                ptr->sender_address, ptr->port_num, ptr->num_packets, ptr->total_collisions, ptr->avg_bw);
                        }
                        closesocket(socket);
                        FD_CLR(socket, &master_set);
                    } else {
                        if (sender_count == 0) {
                            memcpy(buffer, temp_buffer, bytes_received);
                            message_len = bytes_received;
                        }

                        ptr = head->next;
                        while (ptr) {
                            if (ptr->socket == socket) {
                                ptr->num_packets++;
                                ptr->sent_in_slot = 1;
                                break;
                            }
                            ptr = ptr->next;
                        }

                        sender_count++;
                    }
                }
            }
        }

        // Process slot outcome
        if (sender_count == 1) {
            ptr = head->next;
            while (ptr) {
                if (FD_ISSET(ptr->socket, &master_set)) {
                    send(ptr->socket, buffer, message_len, 0);
                }
                ptr = ptr->next;
            }
        } else if (sender_count > 1) {
            char *noise = "!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!\n";
            ptr = head->next;
            while (ptr) {
                if (ptr->sent_in_slot) {
                    ptr->total_collisions++;
                }
                if (FD_ISSET(ptr->socket, &master_set)) {
                    send(ptr->socket, noise, strlen(noise), 0);
                }
                ptr = ptr->next;
            }
        }
    }

    closesocket(tcp_s);
    WSACleanup();
    free(c1);
    free_list(head);
    return 0;
}