# TCP File Transfer Application (WinSock)

A client-server TCP file transfer application implemented in C using Windows Sockets (WinSock).

This project demonstrates low-level network programming, custom protocol design, CRC-based error detection, and reliable data transfer using the Stop-and-Wait ARQ protocol.

---

## 📌 Project Overview

This application implements a file transfer system over TCP using a custom frame-based protocol.

- Platform: Windows  
- Networking API: WinSock  
- Port: 5050  
- Protocol: Custom frame-based protocol  
- Reliability Mechanism: Stop-and-Wait ARQ  
- Error Detection: CRC validation  
- Maximum Payload Size: 1400 bytes per frame  

---

## 🧠 Architecture

The system follows a client-server architecture:

- `server.c` → Listens on port 5050 and receives file frames  
- `client.c` → Connects to the server and sends file frames  
- `protocol.h` → Defines frame structure  
- `common.c / common.h` → Shared utility functions  

---

## 📦 File Transfer Logic

1. The file is divided into frames of maximum 1400 bytes (MAX_PAYLOAD).
2. Each frame contains:
   - Payload
   - CRC checksum
   - Sequence number
3. The sender transmits one frame at a time.
4. The receiver:
   - Verifies the CRC
   - Sends ACK or NACK
5. Stop-and-Wait ARQ ensures reliability:
   - If ACK received → send next frame
   - If NACK or timeout → retransmit frame

This guarantees reliable file transmission even in the presence of transmission errors.

---

## ▶ How to Compile (Windows)

Using MinGW or a compatible C compiler:

```bash
gcc server.c common.c -o server -lws2_32
gcc client.c common.c -o client -lws2_32
```

---

## ▶ How to Run

1. Start the server:

```bash
server.exe
```

2. Start the client:

```bash
client.exe
```

The client connects to the server on port 5050 and transfers the selected file using the custom protocol.

---

## 🔍 Concepts Demonstrated

- Socket Programming (WinSock)
- TCP Client-Server Architecture
- Custom Protocol Design
- CRC Error Detection
- Stop-and-Wait ARQ
- Frame-based Data Transmission
- Reliable Communication Mechanisms

---

## 🎯 Purpose

This project was built to demonstrate low-level network programming and protocol implementation in C, focusing on reliability, error control, and structured data transmission over TCP.
