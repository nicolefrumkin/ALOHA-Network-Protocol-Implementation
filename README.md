# ALOHA Network Protocol Implementation

## Overview
This project implements a simple network communication system where multiple servers send files through a shared channel using an ALOHA-like protocol. The system handles collision detection and uses exponential backoff for retry attempts.

## Files

- `channel.c` – Acts as a central communication channel that receives and forwards messages between servers. It detects collisions and reports stats like number of packets, collisions, and bandwidth.
- `server.c` – Reads a file, splits it into frames, and sends them to the channel. It handles timeouts and retries using exponential backoff.

## How to Use

### Compile

Make sure you have a Windows environment with Winsock2.

```bash
gcc channel.c -o channel.exe -lws2_32
gcc server.c -o server.exe -lws2_32
```

### Run

1. Start the channel:
   ```bash
   channel <chan_port> <slot_time>
   ```

2. Start the server:
   ```bash
   server <chan_ip> <chan_port> <file_name> <frame_size> <slot_time> <seed> <timeout>
   ```

## Features

- Simple TCP-based communication
- Simulated Ethernet frames
- Handles packet collisions using randomized backoff
- Bandwidth and performance stats
