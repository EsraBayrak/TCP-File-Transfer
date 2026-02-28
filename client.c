

#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>


#pragma comment(lib, "ws2_32.lib")

#include "protocol.h"
#include "common.h"

/*  CLIENT-ONLY CONFIG*/
#define OUT_PREFIX "received_"
#define CSV_PATH   "results.csv"

/* Shannon-Hartley demonstration */
#define DEMO_BANDWIDTH_HZ 1000000.0 /* 1 MHz */

/* CHANNEL SIM (SNR -> BER -> bit flips)  */
static double ber_from_snr_db(double snr_db) {
    double snr_lin = pow(10.0, snr_db / 10.0);
    double pb = 0.5 * erfc(sqrt(snr_lin));
    if (pb < 1e-12) pb = 1e-12;
    if (pb > 0.5) pb = 0.5;
    return pb;
}

static double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static void apply_noise_count(uint8_t* buf, int len, double p_bit,
                              unsigned long long* flipped_bits,
                              unsigned long long* total_bits_seen) {
    if (len <= 0) return;

    *total_bits_seen += (unsigned long long)len * 8ULL;
    if (p_bit <= 0.0) return;

    for (int i = 0; i < len; i++) {
        uint8_t b = buf[i];
        for (int bit = 0; bit < 8; bit++) {
            double r = (double)rand() / (double)RAND_MAX;
            if (r < p_bit) {
                b ^= (uint8_t)(1u << bit);
                (*flipped_bits)++;
            }
        }
        buf[i] = b;
    }
}

/*  SHANNON */
static double shannon_hartley_mbps(double snr_db) {
    double snr_lin = pow(10.0, snr_db / 10.0);
    double cap_bps = DEMO_BANDWIDTH_HZ * (log(1.0 + snr_lin) / log(2.0));
    return cap_bps / 1e6;
}

static void shannon_hartley_print(double snr_db) {
    double snr_lin = pow(10.0, snr_db / 10.0);
    double cap_bps = DEMO_BANDWIDTH_HZ * (log(1.0 + snr_lin) / log(2.0));
    printf("Shannon-Hartley capacity (B=%.0f Hz): %.2f bps (%.2f Mbps)\n",
           DEMO_BANDWIDTH_HZ, cap_bps, cap_bps / 1e6);
}

/*  FRAMING (client: recv)  */
static int recv_frame(SOCKET s, FrameHeader* out_h, uint8_t* out_payload, uint32_t maxlen) {
    FrameHeader h;
    if (!recv_all(s, &h, (int)sizeof(h))) return 0;

    if (h.len > maxlen) {
        uint8_t tmp[256];
        uint32_t remain = h.len;
        while (remain > 0) {
            int take = (remain > (uint32_t)sizeof(tmp)) ? (int)sizeof(tmp) : (int)remain;
            if (!recv_all(s, tmp, take)) break;
            remain -= (uint32_t)take;
        }
        *out_h = h;
        return 1;
    }

    if (h.len > 0) {
        if (!recv_all(s, out_payload, (int)h.len)) return 0;
    }
    *out_h = h;
    return 1;
}

/*  ACK/NAK (text line) */
static int send_acknak(SOCKET s, int ack, uint32_t seq) {
    char line[64];
    snprintf(line, sizeof(line), "%s %u\n", ack ? "ACK" : "NAK", (unsigned)seq);
    return send_all(s, line, (int)strlen(line));
}

/*  OUTPUT NAME */
static void make_out_name(char* outName, size_t cap) {
    time_t t = time(NULL);
    struct tm* tmv = localtime(&t);
    if (!tmv) {
        snprintf(outName, cap, OUT_PREFIX "output.jpg");
        return;
    }
    snprintf(outName, cap, OUT_PREFIX "%04d%02d%02d_%02d%02d%02d.jpg",
             tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
             tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
}

/*  CSV LOGGING  */
static int file_exists(const char* path) {
    return (_access(path, 0) == 0);
}

static void csv_append_result(
    const char* csv_path,
    const char* timestamp,
    const char* server_ip,
    int port,
    double snr_db,
    int scale,
    double ber_theory,
    double p_bit,
    int arq_on,
    int frames_ok,
    int crc_fails,
    long written_bytes,
    long total_bytes,
    double elapsed_s,
    double throughput_mbps,
    double shannon_mbps,
    double efficiency_pct,
    unsigned long long flipped_bits,
    unsigned long long total_bits_seen,
    double ber_measured
) {
    int need_header = !file_exists(csv_path);

    FILE* f = fopen(csv_path, "a");
    if (!f) {
        printf("WARNING: Cannot open %s for logging.\n", csv_path);
        return;
    }

    if (need_header) {
        fprintf(f,
            "timestamp,server_ip,port,"
            "snr_db,scale,ber_theory,p_bit,"
            "arq_on,frames_ok,crc_fails,"
            "written_bytes,total_bytes,elapsed_s,"
            "throughput_mbps,shannon_mbps,efficiency_pct,"
            "flipped_bits,total_bits_seen,ber_measured\n"
        );
    }

    fprintf(f,
        "%s,%s,%d,"
        "%.4f,%d,%.6e,%.6e,"
        "%d,%d,%d,"
        "%ld,%ld,%.6f,"
        "%.6f,%.6f,%.6f,"
        "%llu,%llu,%.6e\n",
        timestamp, server_ip, port,
        snr_db, scale, ber_theory, p_bit,
        arq_on, frames_ok, crc_fails,
        written_bytes, total_bytes, elapsed_s,
        throughput_mbps, shannon_mbps, efficiency_pct,
        (unsigned long long)flipped_bits,
        (unsigned long long)total_bits_seen,
        ber_measured
    );

    fclose(f);
}

/* ENTRYPOINT*/
int client_main(void) {
    winsock_init();
    srand((unsigned)time(NULL));
    crc32_init();

    char serverIp[64];
    printf("Server IP (same PC: 127.0.0.1): ");
    fflush(stdout);
    if (!fgets(serverIp, sizeof(serverIp), stdin)) die("input failed");
    serverIp[strcspn(serverIp, "\r\n")] = 0;
    if (serverIp[0] == '\0') strcpy(serverIp, "127.0.0.1");

    double snr_db = 10.0;
    int scale = 3000;
    int use_arq = 1;

    printf("SNR(dB): ");
    fflush(stdout);
    if (scanf("%lf", &snr_db) != 1) die("bad SNR input");

    printf("SCALE (noise amplification,  1000..5000): ");
    fflush(stdout);
    if (scanf("%d", &scale) != 1) die("bad SCALE input");

    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}

    printf("Enable Error Correction (ARQ)? (1:Yes, 0:No - choose 0 to see corrupted output): ");
    fflush(stdout);
    if (scanf("%d", &use_arq) != 1) die("bad ARQ input");
    while ((ch = getchar()) != '\n' && ch != EOF) {}
    if (use_arq != 0) use_arq = 1;

    shannon_hartley_print(snr_db);
    double shannon_mbps = shannon_hartley_mbps(snr_db);

    double ber_theory = ber_from_snr_db(snr_db);
    double p_bit = clamp01(ber_theory * ((double)scale / 1000.0));
    printf("Derived BER approx (BPSK/AWGN): %.3e\n", ber_theory);
    printf("Effective bit-flip probability p_bit ~= %.3e\n\n", p_bit);

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) die("socket() failed");

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)SERVER_PORT);

    unsigned long ip = inet_addr(serverIp);
    if (ip == INADDR_NONE) die("Invalid IP address");
    sa.sin_addr.s_addr = ip;

    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        die("connect() failed");
    }

    char line[MAX_LINE];
    if (!recv_line(s, line, (int)sizeof(line))) die("server closed early");

    if (strncmp(line, "ERR:", 4) == 0) {
        printf("%s", line);
        closesocket(s);
        winsock_cleanup();
        return 0;
    }

    long total_bytes = 0;
    if (sscanf(line, "SIZE %ld", &total_bytes) != 1) die("bad SIZE line");
    printf("Incoming file size: %ld bytes\n", total_bytes);

    {
        char arqLine[32];
        snprintf(arqLine, sizeof(arqLine), "ARQ %d\n", use_arq);
        if (!send_all(s, arqLine, (int)strlen(arqLine))) die("failed to send ARQ mode");
    }

    

    char fname[256];
    make_out_name(fname, sizeof(fname));

    char outName[512];
    snprintf(outName, sizeof(outName), "%s", fname);   

    FILE* out = fopen(outName, "wb");

    if (!out) die("cannot create output file");

    printf("Saving to: %s\n", outName);
    printf("ARQ mode: %s\n\n", use_arq ? "ON (ACK/NAK + CRC)" : "OFF (write corrupted payload)");

    uint8_t payload[MAX_PAYLOAD];
    long written = 0;

    int frames_ok = 0;
    int crc_fails = 0;
    uint32_t expected_seq = 0;

    unsigned long long flipped_bits = 0;
    unsigned long long total_bits_seen = 0;

    DWORD t0 = GetTickCount();

    while (1) {
        FrameHeader h;
        int ok = recv_frame(s, &h, payload, MAX_PAYLOAD);
        if (!ok) {
            printf("Socket closed.\n");
            break;
        }

        if (h.magic == FRAME_MAGIC && h.len == 0) {
            break;
        }

        apply_noise_count(payload, (int)h.len, p_bit, &flipped_bits, &total_bits_seen);

        if (h.magic != FRAME_MAGIC || h.len > MAX_PAYLOAD) {
            printf("Frame receive/decode failed (bad magic/len).\n");
            break;
        }

        if (!use_arq) {
            xor_apply(payload, (int)h.len, (uint8_t)XOR_KEY);
            if (h.len > 0) {
                size_t w = fwrite(payload, 1, h.len, out);
                written += (long)w;
            }
            frames_ok++;
            expected_seq++;
            printf("\r[RECV] %0.2f%%",
                   (total_bytes > 0) ? (double)written * 100.0 / (double)total_bytes : 0.0);
            fflush(stdout);
            continue;
        }

        uint32_t want = h.crc;
        uint32_t got  = frame_crc(&h, payload);

        if (got != want || h.seq != expected_seq) {
            crc_fails++;
            (void)send_acknak(s, 0, h.seq);
            continue;
        }

        (void)send_acknak(s, 1, h.seq);
        xor_apply(payload, (int)h.len, (uint8_t)XOR_KEY);

        if (h.len > 0) {
            size_t w = fwrite(payload, 1, h.len, out);
            written += (long)w;
        }

        frames_ok++;
        expected_seq++;

        printf("\r[RECV] %0.2f%%",
               (total_bytes > 0) ? (double)written * 100.0 / (double)total_bytes : 0.0);
        fflush(stdout);
    }

    DWORD t1 = GetTickCount();
    fclose(out);
    closesocket(s);
    winsock_cleanup();

    printf("\n\nCLIENT SUMMARY\n");
    printf("Frames OK/Written: %d\n", frames_ok);

    if (use_arq) {
        printf("CRC fails (NAK sent): %d\n", crc_fails);
        printf("Written bytes: %ld / %ld\n", written, total_bytes);
        printf("ARQ active: NAK triggers retransmission until CRC OK.\n");
    } else {
        printf("ARQ OFF: CRC/ACK/NAK disabled; output may be corrupted (demo).\n");
        printf("Written bytes: %ld / %ld\n", written, total_bytes);
    }

    double elapsed_s = (double)(t1 - t0) / 1000.0;
    if (elapsed_s <= 0.0) elapsed_s = 0.001;

    double throughput_mbps = ((double)written * 8.0) / (elapsed_s * 1e6);

    double efficiency_pct = 0.0;
    if (shannon_mbps > 0.0) efficiency_pct = (throughput_mbps / shannon_mbps) * 100.0;
    if (efficiency_pct < 0.0) efficiency_pct = 0.0;

    double ber_measured = 0.0;
    if (total_bits_seen > 0ULL) ber_measured = (double)flipped_bits / (double)total_bits_seen;

    printf("Elapsed: %.3f s\n", elapsed_s);
    printf("Throughput: %.3f Mbps\n", throughput_mbps);
    printf("Shannon limit: %.3f Mbps\n", shannon_mbps);
    printf("Efficiency: %.2f %% (throughput/shannon*100)\n", efficiency_pct);
    printf("Measured BER (bit flips / bits seen): %.3e\n", ber_measured);

    char ts[32];
    {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        if (t) {
            snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
                     t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                     t->tm_hour, t->tm_min, t->tm_sec);
        } else {
            strcpy(ts, "unknown");
        }
    }

    csv_append_result(
        CSV_PATH,
        ts,
        serverIp,
        SERVER_PORT,
        snr_db,
        scale,
        ber_theory,
        p_bit,
        use_arq,
        frames_ok,
        crc_fails,
        written,
        total_bytes,
        elapsed_s,
        throughput_mbps,
        shannon_mbps,
        efficiency_pct,
        flipped_bits,
        total_bits_seen,
        ber_measured
    );

    printf("\nCSV log appended -> %s\n", CSV_PATH);
    printf("Done. Press Enter to exit...\n");
    getchar();

    return 0;
}


