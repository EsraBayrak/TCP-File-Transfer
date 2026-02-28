#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "protocol.h"
#include "common.h"

/* UTIL  */
void die(const char* msg) {
    fprintf(stderr, "FATAL: %s\n", msg);
    exit(1);
}

/*  WINSOCK */
void winsock_init(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) die("WSAStartup failed");
}

void winsock_cleanup(void) {
    WSACleanup();
}

/*IO */
int send_all(SOCKET s, const void* buf, int len) {
    const char* p = (const char*)buf;
    int sent = 0;
    while (sent < len) {
        int r = send(s, p + sent, len - sent, 0);
        if (r <= 0) return 0;
        sent += r;
    }
    return 1;
}

int recv_all(SOCKET s, void* buf, int len) {
    char* p = (char*)buf;
    int recvd = 0;
    while (recvd < len) {
        int r = recv(s, p + recvd, len - recvd, 0);
        if (r <= 0) return 0;
        recvd += r;
    }
    return 1;
}

int recv_line(SOCKET s, char* out, int maxlen) {
    int n = 0;
    while (n < maxlen - 1) {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r <= 0) return 0;
        out[n++] = c;
        if (c == '\n') break;
    }
    out[n] = '\0';
    return 1;
}

/* CRC32 */
static uint32_t crc32_table[256];
static int crc32_inited = 0;

void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_inited = 1;
}

uint32_t crc32_calc(const void* data, size_t len) {
    if (!crc32_inited) crc32_init();
    const uint8_t* p = (const uint8_t*)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c = crc32_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* XOR  */
void xor_apply(uint8_t* buf, int len, uint8_t key) {
    for (int i = 0; i < len; i++) buf[i] ^= key;
}

/* FRAME CRC*/
uint32_t frame_crc(const FrameHeader* h, const uint8_t* payload) {
    FrameHeader tmp = *h;
    tmp.crc = 0;

    if (!crc32_inited) crc32_init();

    uint32_t c = 0xFFFFFFFFu;
    const uint8_t* ph = (const uint8_t*)&tmp;

    for (size_t i = 0; i < sizeof(FrameHeader); i++) {
        c = crc32_table[(c ^ ph[i]) & 0xFFu] ^ (c >> 8);
    }
    for (uint32_t i = 0; i < tmp.len; i++) {
        c = crc32_table[(c ^ payload[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

