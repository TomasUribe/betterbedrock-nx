// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink relay core. See relay_core.h.
#include "relay_core.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const uint8_t k_magic[16] = {0x00, 0xff, 0xff, 0x00, 0xfe, 0xfe, 0xfe, 0xfe,
                                    0xfd, 0xfd, 0xfd, 0xfd, 0x12, 0x34, 0x56, 0x78};
#define ID_PONG 0x1c
#define ID_OPEN_REQ_1 0x05
#define UDP_IP_OVERHEAD 28

size_t relay_rewrite_pong(const uint8_t *in, size_t n, uint8_t *out, size_t cap, uint16_t port) {
    if (n < 35 || in[0] != ID_PONG || memcmp(in + 17, k_magic, 16) != 0) return 0;
    size_t slen = ((size_t)in[33] << 8) | in[34];
    if (35 + slen > n || slen > 1024) return 0;
    // MCPE;name;protocol;version;players;max;guid;sub;mode;modeId;port4;port6;
    char motd[1100], fixed[1200], portstr[8];
    memcpy(motd, in + 35, slen);
    motd[slen] = '\0';
    const char *f[32];
    int nf = 0;
    char *p = motd;
    f[nf++] = p;
    while (nf < 32 && (p = strchr(p, ';')) != NULL) {
        *p++ = '\0';
        f[nf++] = p;
    }
    while (nf < 12) f[nf++] = "";
    snprintf(portstr, sizeof portstr, "%u", port);
    f[10] = f[11] = portstr;
    size_t o = 0;
    for (int i = 0; i < nf; i++) {
        size_t l = strlen(f[i]);
        if (o + l + 1 >= sizeof fixed) return 0;
        memcpy(fixed + o, f[i], l);
        o += l;
        if (i + 1 < nf) fixed[o++] = ';';
    }
    if (35 + o > cap) return 0;
    memcpy(out, in, 33);
    out[33] = (uint8_t)(o >> 8);
    out[34] = (uint8_t)o;
    memcpy(out + 35, fixed, o);
    return 35 + o;
}

static void set_nonblocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int relay_open(relay *r, const char *bind_ip, uint16_t port, const struct sockaddr_in *remote, relay_log_fn log) {
    memset(r, 0, sizeof *r);
    r->listen_fd = -1;
    r->local_port = port;
    r->remote = *remote;
    r->log = log;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -errno;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_ip, &a.sin_addr) != 1) {
        close(fd);
        return -EINVAL;
    }
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        int e = errno;
        close(fd);
        return -e;
    }
    set_nonblocking(fd);
    r->listen_fd = fd;
    return 0;
}

static const char *packet_name(uint8_t id) {
    switch (id) {
        case 0x01: return "ping";
        case 0x05: return "open-req-1";
        case 0x06: return "open-reply-1";
        case 0x07: return "open-req-2";
        case 0x08: return "open-reply-2";
        case 0x19: return "incompatible-protocol";
        case 0x1c: return "pong";
        case 0xa0: return "nack";
        case 0xc0: return "ack";
        default: return (id & 0x80) ? "frames" : "other";
    }
}

static void note_packet(relay *r, relay_session *s, const char *dir, const uint8_t *pkt, size_t len) {
    if (!r->log || s->logged >= RELAY_LOG_PACKETS) return;
    s->logged++;
    r->log("  :%u %s %s 0x%02x %u B", ntohs(s->client.sin_port), dir, packet_name(pkt[0]), pkt[0], (unsigned)len);
}

static void end_session(relay *r, relay_session *s, const char *why) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &s->client.sin_addr, ip, sizeof ip);
    if (r->log)
        r->log("session %s:%u ended (%s): %llu bytes up, %llu down, %u MTU probes cut, %u send / %u receive errors",
               ip, ntohs(s->client.sin_port), why, (unsigned long long)s->up, (unsigned long long)s->down,
               s->clamped, s->up_err, s->down_err);
    close(s->fd);
    s->used = 0;
}

static relay_session *session_for(relay *r, const struct sockaddr_in *from, uint64_t now_ms) {
    relay_session *free_slot = NULL, *oldest = NULL;
    for (int i = 0; i < RELAY_MAX_SESSIONS; i++) {
        relay_session *s = &r->s[i];
        if (!s->used) {
            if (!free_slot) free_slot = s;
            continue;
        }
        if (s->client.sin_addr.s_addr == from->sin_addr.s_addr && s->client.sin_port == from->sin_port) return s;
        if (!oldest || s->last_ms < oldest->last_ms) oldest = s;
    }
    if (!free_slot) {
        end_session(r, oldest, "replaced");
        free_slot = oldest;
    }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;
    if (connect(fd, (const struct sockaddr *)&r->remote, sizeof r->remote) != 0) {
        close(fd);
        return NULL;
    }
    set_nonblocking(fd);
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->used = 1;
    free_slot->fd = fd;
    free_slot->client = *from;
    free_slot->last_ms = now_ms;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &from->sin_addr, ip, sizeof ip);
    if (r->log) r->log("session %s:%u started", ip, ntohs(from->sin_port));
    return free_slot;
}

int relay_step(relay *r, int timeout_ms, uint64_t now_ms) {
    struct pollfd p[1 + RELAY_MAX_SESSIONS];
    relay_session *who[1 + RELAY_MAX_SESSIONS];
    int n = 0;
    p[n].fd = r->listen_fd;
    p[n].events = POLLIN;
    p[n].revents = 0;
    who[n++] = NULL;
    for (int i = 0; i < RELAY_MAX_SESSIONS; i++) {
        if (!r->s[i].used) continue;
        p[n].fd = r->s[i].fd;
        p[n].events = POLLIN;
        p[n].revents = 0;
        who[n++] = &r->s[i];
    }
    int rc = poll(p, (nfds_t)n, timeout_ms);
    if (rc < 0) return errno == EINTR ? 0 : -1;

    static uint8_t buf[2048], out[2048];
    int errors = 0;
    if (p[0].revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
    if (p[0].revents & POLLIN) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        ssize_t got = recvfrom(r->listen_fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK) errors++;
        // Only this console may use the relay: 127.0.0.1 or its own address. Until that
        // address is known (no network yet), nothing else can reach it anyway.
        int local = from.sin_addr.s_addr == htonl(INADDR_LOOPBACK) || !r->own_ip || from.sin_addr.s_addr == r->own_ip;
        if (got > 0 && !local) {
            if (++r->dropped_foreign <= 3 && r->log) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
                r->log("ignored a packet from %s: only this console may use the relay", ip);
            }
        } else if (got > 0) {
            relay_session *s = session_for(r, &from, now_ms);
            if (s) {
                size_t len = (size_t)got;
                if (buf[0] == ID_OPEN_REQ_1 && len + UDP_IP_OVERHEAD > RELAY_MAX_MTU) {
                    len = RELAY_MAX_MTU - UDP_IP_OVERHEAD;  // the server answers with this smaller MTU
                    s->clamped++;
                }
                s->last_ms = now_ms;
                s->up += len;
                note_packet(r, s, "up  ", buf, len);
                // Errors here (EMSGSIZE, ECONNREFUSED) just drop the packet; RakNet resends.
                if (send(s->fd, buf, len, 0) < 0) {
                    errors++;
                    if (++s->up_err <= 3 && r->log)
                        r->log("  :%u send of %u B failed, errno %d", ntohs(s->client.sin_port), (unsigned)len, errno);
                }
            }
        }
    }
    for (int i = 1; i < n; i++) {
        relay_session *s = who[i];
        if (!s->used || p[i].fd != s->fd) continue;  // ended or replaced during this step
        if (p[i].revents & (POLLHUP | POLLNVAL)) {
            errors++;
            end_session(r, s, "socket closed");
            continue;
        }
        if (!(p[i].revents & (POLLIN | POLLERR))) continue;
        ssize_t got = recv(s->fd, buf, sizeof buf, 0);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            errors++;
            int e = errno;
            if (++s->down_err <= 3 && r->log) r->log("  :%u receive error, errno %d", ntohs(s->client.sin_port), e);
            // EMSGSIZE / ECONNREFUSED are one-off ICMP reports RakNet copes with. Anything else
            // (network down after a Wi-Fi change, ...) ends the session: polling it again would
            // return the same error at once, forever.
            if (e != EMSGSIZE && e != ECONNREFUSED) end_session(r, s, "network error");
            continue;
        }
        if (got == 0) continue;
        s->last_ms = now_ms;
        s->down += (uint64_t)got;
        note_packet(r, s, "down", buf, (size_t)got);
        size_t len = relay_rewrite_pong(buf, (size_t)got, out, sizeof out, r->local_port);
        const uint8_t *send_buf = len ? out : buf;
        if (!len) len = (size_t)got;
        sendto(r->listen_fd, send_buf, len, 0, (const struct sockaddr *)&s->client, sizeof s->client);
    }
    for (int i = 0; i < RELAY_MAX_SESSIONS; i++)
        if (r->s[i].used && now_ms - r->s[i].last_ms > RELAY_IDLE_MS) end_session(r, &r->s[i], "idle");
    return errors;
}

void relay_close(relay *r) {
    for (int i = 0; i < RELAY_MAX_SESSIONS; i++)
        if (r->s[i].used) end_session(r, &r->s[i], "closing");
    if (r->listen_fd >= 0) close(r->listen_fd);
    r->listen_fd = -1;
}
