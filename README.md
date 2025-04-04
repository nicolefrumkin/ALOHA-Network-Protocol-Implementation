# ALOHA Network Protocol Implementation

## Overview
This project implements a simple network communication system where multiple servers send files through a shared channel using an ALOHA-like protocol. The system handles collision detection and uses exponential backoff for retry attempts.

## Components
- **Server**: Sends files through the channel and manages retransmissions when collisions occur
- **Channel**: Receives and broadcasts messages, detects collisions, and reports statistics

## How to Run

### Channel
```
./my_channel.exe <chan_port> <slot_time>
```
- `chan_port`: Port number for the channel to listen on
- `slot_time`: Wait time in milliseconds

### Server
```
./my_Server.exe <chan_ip> <chan_port> <file_name> <frame_size> <slot_time> <seed> <timeout>
```
- `chan_ip`: IP address of the channel
- `chan_port`: Port number of the channel
- `file_name`: Name of the file to send
- `frame_size`: Maximum packet size in bytes (including header)
- `slot_time`: Wait time unit in milliseconds for collision cases
- `seed`: Seed for random number generator
- `timeout`: Maximum wait time in seconds before declaring failure

## Features
- TCP socket communication
- Collision detection and handling
- Exponential backoff algorithm
- Performance metrics (bandwidth, transmission counts, success rate)
