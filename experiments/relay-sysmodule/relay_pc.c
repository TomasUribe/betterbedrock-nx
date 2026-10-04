// SPDX-License-Identifier: GPL-2.0-or-later
// Runs relay/source/relay_core.c on a PC, or unit-tests its pong rewrite.
//   relay_pc --test
//   relay_pc <remote ip> <remote port> <local port>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#include "../relay/source/relay_core.h"

static void log_stdout(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static const uint8_t k_magic[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                                    0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};

static size_t make_pong(uint8_t *b, const char *motd) {
    memset(b, 0, 35);
    b[0] = 0x1c;
    memcpy(b + 17, k_magic, 16);
    size_t l = strlen(motd);
    b[33] = (uint8_t)(l >> 8);
    b[34] = (uint8_t)l;
    memcpy(b + 35, motd, l);
    return 35 + l;
}

static int check(const char *in, const char *want) {
    uint8_t a[1200], b[1200];
    size_t n = make_pong(a, in);
    size_t m = relay_rewrite_pong(a, n, b, sizeof b, 19132);
    char got[1200] = "";
    if (m >= 35) {
        memcpy(got, b + 35, m - 35);
        got[m - 35] = '\0';
    }
    int ok = m && !strcmp(got, want) && !memcmp(a, b, 33);
    printf("%s  %s\n", ok ? "ok  " : "FAIL", ok ? want : got);
    return ok;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--test")) {
        int ok = 1;
        ok &= check("MCPE;My Server (Season 2!);2169;26.45;0;20;1234567890123456789;Another Geyser server.;Survival;1;40123;40123;",
                    "MCPE;My Server (Season 2!);2169;26.45;0;20;1234567890123456789;Another Geyser server.;Survival;1;19132;19132;");
        ok &= check("MCPE;x;1;1.0;0;10;1;sub;Survival;1;40123;40123",
                    "MCPE;x;1;1.0;0;10;1;sub;Survival;1;19132;19132");
        ok &= check("MCPE;short;1;1.0;0;10", "MCPE;short;1;1.0;0;10;;;;;19132;19132");
        uint8_t junk[40] = {0x05};
        uint8_t out[64];
        ok &= relay_rewrite_pong(junk, sizeof junk, out, sizeof out, 19132) == 0;
        // A session whose socket goes bad must be closed, not polled again in a loop.
        {
            struct sockaddr_in up = {0};
            up.sin_family = AF_INET;
            up.sin_port = htons(9);
            inet_pton(AF_INET, "127.0.0.1", &up.sin_addr);
            relay r;
            int orc = relay_open(&r, "127.0.0.1", 19151, &up, log_stdout);
            int cl = socket(AF_INET, SOCK_DGRAM, 0);
            struct sockaddr_in to = up;
            to.sin_port = htons(19151);
            sendto(cl, "\x84hello", 6, 0, (struct sockaddr *)&to, sizeof to);
            relay_step(&r, 200, now_ms());
            int had = r.s[0].used;
            close(r.s[0].fd);  // the network took it away
            int steps = 0, total_err = 0;
            for (; steps < 50 && r.s[0].used; steps++) {
                int e = relay_step(&r, 200, now_ms());
                if (e > 0) total_err += e;
            }
            int good = orc == 0 && had && !r.s[0].used && steps <= 2;
            printf("%s  broken session closed after %d step(s), %d error(s)\n", good ? "ok  " : "FAIL", steps, total_err);
            ok &= good;
            relay_close(&r);
            close(cl);
        }
        printf("relay core: %s\n", ok ? "all ok" : "FAILED");
        return !ok;
    }
    if (argc != 4) {
        fprintf(stderr, "usage: %s --test | <remote ip> <remote port> <local port>\n", argv[0]);
        return 2;
    }
    struct sockaddr_in remote = {0};
    remote.sin_family = AF_INET;
    remote.sin_port = htons((uint16_t)atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &remote.sin_addr);
    relay r;
    int rc = relay_open(&r, "127.0.0.1", (uint16_t)atoi(argv[3]), &remote, log_stdout);
    if (rc) {
        fprintf(stderr, "relay_open: %s\n", strerror(-rc));
        return 1;
    }
    log_stdout("relay on 127.0.0.1:%s -> %s:%s", argv[3], argv[1], argv[2]);
    for (;;)
        if (relay_step(&r, 1000, now_ms()) < 0) {
            log_stdout("listen socket broke");
            return 1;
        }
}
