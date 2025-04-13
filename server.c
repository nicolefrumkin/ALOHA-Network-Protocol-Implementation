#include "winsock2.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef struct Input
{
    char *chan_ip;
    int chan_port;
    char *file_name;
    int frame_size;
    int slot_time;
    int seed;
    int timeout;
} Input;

typedef struct Output
{
    char *file_name;
    int success;
    int file_size;
    int num_of_packets;
    int total_time;
    int max_transmissions;
    double avg_transmissions;
    double avg_bw;
} Output;

void exponential_backoff(int k, int slot_time, int *seed)
{
    int r = rand(seed) % (1 << k);
    Sleep(r * slot_time);
}

int main(int argc, char *argv[])
{
    Input *s1 = (Input *)malloc(sizeof(Input));
    Output *out = (Output *)malloc(sizeof(Output));

    if (argc != 8)
    {
        fprintf(stderr, "Usage: %s <chan_ip> <chan_port> <file_name> <frame_size> <slot_time> <seed> <timeout>\n", argv[0]);
        free(s1);
        free(out);
        return 1;
    }
    s1->chan_ip = argv[1];
    s1->chan_port = atoi(argv[2]);
    s1->file_name = argv[3];
    s1->frame_size = atoi(argv[4]);
    s1->slot_time = atoi(argv[5]);
    s1->seed = atoi(argv[6]);
    s1->timeout = atoi(argv[7]);

    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR)
        printf("Error at WSAStartup()\n");

    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(s1->chan_port);
    server_addr.sin_addr.s_addr = inet_addr(s1->chan_ip);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("connection failed");
        free(s1);
        free(out);
        return 1;
    }
    FILE *f = fopen(s1->file_name, "rb");
    if (!f)
    {
        printf("Failed to open file");
        free(s1);
        free(out);
        return 1;
    }
    char *frame = (char *)malloc(s1->frame_size);
    char *recived = (char *)malloc(s1->frame_size);
    int not_send = 1;
    int collisions = 0;
    int transmissions = 0;
    int total_transmissions = 0;
    int num_frames = 0;
    out->success = 1;
    DWORD start_time = GetTickCount();
    DWORD curr_time = 0; 
    while (!feof(f))
    {
        size_t read_bytes = fread(frame, 1, s1->frame_size, f);
        while (not_send)
        {
            DWORD start_frame_time = GetTickCount();
            send(sockfd, frame, read_bytes, 0);
            while (recv(sockfd, recived, s1->frame_size, 0)<= 0)
            {
                curr_time = GetTickCount();
                if (((curr_time - start_frame_time) > (s1->timeout) * 1000)) {
                    collisions++;
                    transmissions++;
                    total_transmissions++;
                    exponential_backoff(collisions, s1->slot_time, &s1->seed);
                    break;
                }
            }
            curr_time = GetTickCount();
            if (!(strcmp(recived, frame) || ((curr_time - start_frame_time) > (s1->timeout) * 1000)))
            {
                collisions++;
                transmissions++;
                total_transmissions++;
                exponential_backoff(collisions, s1->slot_time, s1->seed);
            }
            if (collisions >= 10)
            {
                out->success = 0;
                break;
            }
            else
            {
                total_transmissions++;
                not_send = 0;
            }
        }
        if (!(out->success)) break;
        not_send = 1;
        if (transmissions > out->max_transmissions)
            out->max_transmissions = transmissions;
        transmissions = 0;
        num_frames++;
        total_transmissions++;
    }
    out->num_of_packets = num_frames;
    out->file_name = s1->file_name;
    out->file_size = num_frames*s1->frame_size;
    out->total_time = GetTickCount()-start_time;
    out->avg_transmissions = (double)total_transmissions / num_frames;
    out->avg_bw = (double) (num_frames*s1->frame_size*8)/(out->total_time*0.001);
    fprintf(stderr,"Sent file %s\n", out->file_name);
    fprintf(stderr,"Result: %s \n", out->success? "Success :)" : "Failure :(");
    fprintf(stderr, "File size: %d Bytes (%d frames)\n",out->file_size, out->num_of_packets);
    fprintf(stderr, "Total transfer time: %d milliseconds", out->total_time);
    fprintf(stderr, "Transmissions/frame: average %.2f, maximum %d\n", out->avg_transmissions, out->max_transmissions);
    fprintf(stderr, "Average bandwidth: %.2f Mbps", out->avg_bw);
    fclose(f);
    free(frame);
    free(recived);
    free(s1);
    free(out);
    return 0;
}