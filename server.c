#include "header.h"

volatile int stop_flag = 0; // Shared flag to signal stop

int main(int argc, char *argv[])
{
    if (argc != 8)
    {
        fprintf(stderr, "Usage: %s <chan_ip> <chan_port> <file_name> <frame_size> <slot_time> <seed> <timeout>\n", argv[0]);
        return 1;
    }
    Input *s1 = (Input *)malloc(sizeof(Input));
    OutputServer *out = (OutputServer *)malloc(sizeof(OutputServer));
    if (!s1 || !out)
    {
        fprintf(stderr, "Memory allocation failed\n");
        if (s1)
            free(s1);
        if (out)
            free(out);
        return 1;
    }
    memset(out, 0, sizeof(OutputServer));
    memset(s1, 0, sizeof(Input));

    s1->chan_ip = argv[1];
    s1->chan_port = atoi(argv[2]);
    s1->file_name = argv[3];
    s1->frame_size = atoi(argv[4]);
    s1->slot_time = atoi(argv[5]);
    s1->seed = atoi(argv[6]);
    s1->timeout = atoi(argv[7]);

    srand(s1->seed);

    // Initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR)
    {
        fprintf(stderr, "Error at WSAStartup(): %d\n", iResult);
        free(s1);
        free(out);
        return 1;
    }

    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET)
    {
        fprintf(stderr, "Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        free(s1);
        free(out);
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(s1->chan_port);
    server_addr.sin_addr.s_addr = inet_addr(s1->chan_ip);

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        fprintf(stderr, "Connection failed: %d\n", WSAGetLastError());
        closesocket(sockfd);
        WSACleanup();
        free(s1);
        free(out);
        return 1;
    }

    // Open file
    FILE *f = fopen(s1->file_name, "rb");
    if (!f)
    {
        fprintf(stderr, "Failed to open file: %s\n", s1->file_name);
        closesocket(sockfd);
        WSACleanup();
        free(s1);
        free(out);
        return 1;
    }

    // Allocate buffers
    char *frame = (char *)malloc(s1->frame_size + 1);
    char *received = (char *)malloc(s1->frame_size + 1);
    if (!frame || !received)
    {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(f);
        closesocket(sockfd);
        WSACleanup();
        if (frame)
            free(frame);
        if (received)
            free(received);
        free(s1);
        free(out);
        return 1;
    }

    int total_transmissions = 0;
    int num_frames = 0;
    out->success = 1;
    DWORD start_time = GetTickCount();

    // Set receive timeout (in milliseconds)
    int timeout_ms = s1->timeout * 1000; // 5 seconds
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR)
    {
        fprintf(stderr, "setsockopt SO_RCVTIMEO failed: %d\n", WSAGetLastError());
    }

    // Set send timeout
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR)
    {
        fprintf(stderr, "setsockopt SO_SNDTIMEO failed: %d\n", WSAGetLastError());
    }

    while (!feof(f) && !stop_flag)
    {
        SetConsoleCtrlHandler(ctrl_handler, TRUE);

        memset(frame, 0, s1->frame_size + 1);
        memset(received, 0, s1->frame_size + 1);

        // Read a frame from the file
        size_t read_bytes = fread(frame, 1, s1->frame_size, f);
        if (read_bytes <= 0)
            break; // EOF or error

        char *packet = malloc(HEADER_SIZE + s1->frame_size + 1);
        memset(packet, 0, HEADER_SIZE + s1->frame_size + 1); // Initialize packet buffer

        if (!packet)
        {
            fprintf(stderr, "Memory allocation failed\n");
            continue; // Try the next frame
        }

        // Build Ethernet header
        // Destination MAC (6 bytes)
        memcpy(packet, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);

        // Append src MAC (6 bytes)
        memcpy(packet + 6, "\x11\x22\x33\x44\x55\x66", 6);

        // Append ethertype (2 bytes, let's say 0x0800)
        packet[12] = 0x08;
        packet[13] = 0x01;

        // Append frame size (4 bytes, big-endian)
        packet[14] = (s1->frame_size >> 24) & 0xFF;
        packet[15] = (s1->frame_size >> 16) & 0xFF;
        packet[16] = (s1->frame_size >> 8) & 0xFF;
        packet[17] = s1->frame_size & 0xFF;

        // Append actual payload
        memcpy(packet + HEADER_SIZE, frame, s1->frame_size);
        frame[s1->frame_size] = '\0';
        packet[HEADER_SIZE + read_bytes] = '\0'; // Null-terminate the packet

        int transmissions = 0;
        int collisions = 0;
        int not_sent = 1;

        // Wait for initial slot
        exponential_backoff(0, s1->slot_time);
        // Attempt to send the frame
        while (not_sent && !stop_flag)
        {
            DWORD start_frame_time = GetTickCount();
            // Send the packet (header + payload)
            int send_result = send(sockfd, packet, HEADER_SIZE + s1->frame_size, 0);
            if (send_result == SOCKET_ERROR)
            {
                fprintf(stderr, "Send failed: %d\n", WSAGetLastError());
                not_sent = 0; // Break out of retry loop
                out->success = 0;
                break;
            }
            transmissions++;
            // Receive response
            int recv_result = recv(sockfd, received, s1->frame_size, 0);
            DWORD curr_time = GetTickCount();

            // Check for timeout
            if (recv_result == SOCKET_ERROR)
            {
                int error = WSAGetLastError();
                if (error == WSAETIMEDOUT)
                {
                    printf("Timeout occurred\n"); // DEBUG
                    collisions++;
                    printf("Collision count: %d, transmissions: %d\n", collisions, transmissions); // DEBUG
                    exponential_backoff(collisions, s1->slot_time);
                    continue;
                }
                else
                {
                    int error = WSAGetLastError();
                    fprintf(stderr, "Receive failed with error code: %d\n", error);
                    not_sent = 0;
                    out->success = 0;
                    break;
                }
            }
            if (recv_result <= 0)
                continue;
            if (strncmp(received, "!!!!!!!!!!!!!!!!!NOISE!!!!!!!!!!!!!!!!!", 39) == 0)
            {
                if (collisions >= 10)
                {
                    printf("Maximum collisions reached for this frame\n");
                    out->success = 0;
                    break;
                }
                printf("NOISE detected - collision occurred\n"); // DEBUG
                collisions++;
                printf("Collision count: %d, transmissions: %d\n", collisions, transmissions);

                // Back off exponentially
                exponential_backoff(collisions, s1->slot_time);
                continue;
            }
            // Check for user interrupt
            if (stop_flag)
                break;

            // Check for too many collisions
            if (collisions >= 10)
            {
                printf("Maximum collisions reached for this frame\n");
                out->success = 0;
                break;
            }
            // Successful transmission if we received our frame back
            if (memcmp(received, frame, recv_result) == 0)
            {
                printf("Frame successfully transmitted\n");
                not_sent = 0;
            }
            else
            {
                printf("Received unexpected data: %s, retrying...\n", received);
                collisions++;
                exponential_backoff(collisions, s1->slot_time);
            }
        }
        // Free packet memory
        free(packet);

        // Break if transmission failed or user interrupted
        if (!out->success || stop_flag)
            break;

        if (transmissions > out->max_transmissions)
            out->max_transmissions = transmissions;
        total_transmissions += transmissions;
        num_frames++;
    }

    printf("finished sending file\n");

    // Fill OutputServer structure
    out->num_of_packets = num_frames;
    out->file_name = s1->file_name;
    out->file_size = num_frames * s1->frame_size;
    out->total_time = GetTickCount() - start_time;

    if (num_frames > 0)
    {
        out->avg_transmissions = (double)total_transmissions / num_frames;
        out->avg_bw = (double)(num_frames * s1->frame_size * 8) / (out->total_time * 1000);
    }
    fprintf(stderr, "\nSent file %s\n", out->file_name);
    fprintf(stderr, "Result: %s \n", out->success ? "Success :)" : "Failure :(");
    fprintf(stderr, "File size: %d Bytes (%d frames)\n", out->file_size, out->num_of_packets);
    fprintf(stderr, "Total transfer time: %d milliseconds\n", out->total_time);
    fprintf(stderr, "Transmissions/frame: average %.3f, maximum %d\n", out->avg_transmissions, out->max_transmissions);
    fprintf(stderr, "Average bandwidth: %.3f Mbps\n\n", out->avg_bw);

    // Clean up
    fclose(f);
    closesocket(sockfd);
    WSACleanup();
    free(frame);
    free(received);
    free(s1);
    // free(out);

    return out->success ? 0 : 1;
}

void exponential_backoff(int k, int slot_time)
{
    int r = rand() % (1 << k);
    Sleep((r * slot_time) % 10000);
}

BOOL WINAPI ctrl_handler(DWORD ctrl_type)
{
    if (ctrl_type == CTRL_C_EVENT)
    {
        stop_flag = 1;
        return TRUE;
    }
    return FALSE;
}