#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>


#define SERVER_PORT 5050

#define FRAME_MAGIC 0x4D434654u 
#define MAX_PAYLOAD 1400
#define MAX_LINE    256

#define XOR_KEY     0xAA


#pragma pack(push, 1)
typedef struct FrameHeader {
    uint32_t magic;
    uint32_t seq;
    uint32_t len;
    uint32_t crc; 
} FrameHeader;
#pragma pack(pop)

#endif

