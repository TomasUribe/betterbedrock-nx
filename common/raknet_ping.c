// SPDX-License-Identifier: GPL-2.0-or-later
// RakNet status ping. See raknet_ping.h.
#include "raknet_ping.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static const uint8_t k_magic[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                                    0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void copy_field(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int raknet_ping(const char *address, unsigned port, int timeout_ms, raknet_status *st) {
    memset(st, 0, sizeof *st);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    if (getaddrinfo(address, ps, &hints, &res) != 0 || !res) return PING_NO_ADDRESS;
    struct sockaddr_in to = *(struct sockaddr_in *)res->ai_addr;
    freeaddrinfo(res);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return PING_SOCKET_ERROR;
    uint8_t ping[33];
    long long t0 = now_ms();
    ping[0] = 0x01;
    for (int i = 0; i < 8; i++) ping[1 + i] = (uint8_t)((unsigned long long)t0 >> (56 - 8 * i));
    memcpy(ping + 9, k_magic, 16);
    for (int i = 0; i < 8; i++) ping[25 + i] = (uint8_t)rand();

    int result = PING_TIMEOUT;
    for (int attempt = 0; attempt < 2 && result == PING_TIMEOUT; attempt++) {
        if (sendto(fd, ping, sizeof ping, 0, (struct sockaddr *)&to, sizeof to) < 0) {
            result = PING_SOCKET_ERROR;
            break;
        }
        long long deadline = now_ms() + timeout_ms / 2;
        for (;;) {
            long long left = deadline - now_ms();
            if (left <= 0) break;
            struct pollfd p = {fd, POLLIN, 0};
            if (poll(&p, 1, (int)left) <= 0) break;
            uint8_t buf[1500];
            ssize_t got = recv(fd, buf, sizeof buf, 0);
            if (got < 35 || buf[0] != 0x1c || memcmp(buf + 17, k_magic, 16) != 0) continue;
            size_t n = ((size_t)buf[33] << 8) | buf[34];
            if (35 + n > (size_t)got) continue;
            char motd[1200];
            memcpy(motd, buf + 35, n);
            motd[n] = '\0';
            // MCPE;name;protocol;version;players;max;...
            char *f[8] = {0};
            int nf = 0;
            for (char *p2 = motd; p2 && nf < 8; nf++) {
                f[nf] = p2;
                p2 = strchr(p2, ';');
                if (p2) *p2++ = '\0';
            }
            copy_field(st->name, sizeof st->name, nf > 1 ? f[1] : "?");
            copy_field(st->version, sizeof st->version, nf > 3 ? f[3] : "?");
            st->players = nf > 4 ? atoi(f[4]) : 0;
            st->max_players = nf > 5 ? atoi(f[5]) : 0;
            st->rtt_ms = (int)(now_ms() - t0);
            result = PING_OK;
            break;
        }
    }
    close(fd);
    return result;
}
