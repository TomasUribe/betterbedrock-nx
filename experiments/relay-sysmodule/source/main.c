// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink relay sysmodule.
//
// While routing is on, the hosts file points one Minecraft featured server at
// this console's own address and this module forwards that traffic to the
// server in sdmc:/config/bedrocklink/server.ini. (Minecraft pings 127.0.0.1 but
// never joins it, so the route line carries the console's Wi-Fi address; when
// that changes, this module updates the line and has dns.mitm reload it.)
// Only packets from this console are relayed, only to that server, and nothing
// runs on a sysMMC boot. It starts at boot through boot2.flag or right away from
// the app or overlay; with routing off it is not running at all.
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <switch.h>

#include "nx_control.h"
#include "relay_core.h"
#include "route.h"

#define RELAY_VERSION "1.2.0"
#define INNER_HEAP_SIZE 0x60000
#define LOG_MAX_BYTES (64 * 1024)
#define START_DELAY_NS 10000000000ULL
#define BOOT_WINDOW_MS 60000          // started later than this = started from the app/overlay: no delay
#define IP_CHECK_MS 3000
#define ERROR_BUDGET_PER_S 200        // more socket errors than this in a second: back off
#define SplConfigItem_ExosphereEmummcType ((SplConfigItem)65007)

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void) {
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void *fake_heap_start;
    extern void *fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
    rc = fsInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    fsdevMountSdmc();
    // sm stays open: socketInitialize needs it later.
}

void __appExit(void) {
    socketExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

static u64 now_ms(void) {
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

static void log_line(const char *fmt, ...) {
    FILE *f = fopen(ROUTE_LOG_PATH, "a");
    if (!f) return;
    u64 ms = now_ms();
    fprintf(f, "[%6llu.%03llu] ", (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static int is_emummc_boot(void) {
    u64 v = 0;
    Result rc = splInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = splGetConfig(SplConfigItem_ExosphereEmummcType, &v);
        splExit();
    }
    if (R_FAILED(rc)) {
        log_line("cannot tell the boot type (spl rc=0x%x): staying off", rc);
        return 0;
    }
    return v != 0;
}

static int resolve(const char *address, unsigned port, struct sockaddr_in *out) {
    memset(out, 0, sizeof *out);
    out->sin_family = AF_INET;
    out->sin_port = htons((u16)port);
    if (inet_pton(AF_INET, address, &out->sin_addr) == 1) return 1;
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(address, NULL, &hints, &res) != 0 || !res) return 0;
    out->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return 1;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/bedrocklink", 0777);
    struct stat st;
    if (stat(ROUTE_LOG_PATH, &st) == 0 && st.st_size > LOG_MAX_BYTES) remove(ROUTE_LOG_PATH);
    log_line("BedrockLink relay " RELAY_VERSION " up (firmware %u.%u.%u)", HOSVER_MAJOR(hosversionGet()),
             HOSVER_MINOR(hosversionGet()), HOSVER_MICRO(hosversionGet()));

    if (!is_emummc_boot()) {
        log_line("not an emuMMC boot: exiting, nothing started");
        return 0;
    }
    route_config cfg;
    char err[96];
    if (!route_load_config(&cfg, err, sizeof err)) {
        log_line("config: %s - exiting", err);
        return 0;
    }
    log_line("config: %s = %s:%u, replaces %s", cfg.name, cfg.address, cfg.port, cfg.replaces);

    if (now_ms() < BOOT_WINDOW_MS) svcSleepThread(START_DELAY_NS);  // let the system finish booting first

    static const SocketInitConfig sock_cfg = {
        .tcp_tx_buf_size = 0x1000,
        .tcp_rx_buf_size = 0x1000,
        .tcp_tx_buf_max_size = 0,
        .tcp_rx_buf_max_size = 0,
        .udp_tx_buf_size = 0x2400,
        .udp_rx_buf_size = 0xA500,
        .sb_efficiency = 2,
        .num_bsd_sessions = 1,
        .bsd_service_type = BsdServiceType_Auto,
    };
    Result rc = 0;
    for (int attempt = 0; attempt < 30; attempt++) {
        rc = socketInitialize(&sock_cfg);
        if (R_SUCCEEDED(rc)) break;
        svcSleepThread(2000000000ULL);
    }
    if (R_FAILED(rc)) {
        log_line("socketInitialize failed rc=0x%x - exiting", rc);
        return 0;
    }
    log_line("sockets up");
    rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc)) log_line("nifm unavailable (rc=0x%x): the route line keeps its address", rc);
    bool have_nifm = R_SUCCEEDED(rc);

    struct sockaddr_in remote;
    for (int attempt = 0; !resolve(cfg.address, cfg.port, &remote); attempt++) {
        if (attempt % 10 == 0) log_line("cannot resolve %s yet, retrying", cfg.address);
        svcSleepThread(30000000000ULL);
    }

    relay r;
    int open = 0, fails = 0;
    u64 next_try = 0, next_ip_check = 0, window_start = now_ms();
    int window_errors = 0;
    char own_ip[32] = "";
    for (;;) {
        u64 now = now_ms();

        // Keep the route line on this console's current address (it changes with the network).
        if (have_nifm && now >= next_ip_check) {
            next_ip_check = now + IP_CHECK_MS;
            char ip[32];
            bl_own_ip(ip, sizeof ip);
            if (strcmp(ip, own_ip) != 0) {
                log_line("console address: %s", ip[0] ? ip : "none (no network)");
                snprintf(own_ip, sizeof own_ip, "%s", ip);
                if (open) inet_pton(AF_INET, ip[0] ? ip : "0.0.0.0", &r.own_ip);
                if (ip[0]) {
                    char msg[160];
                    int u = route_update_ip(1, ip, msg, sizeof msg);
                    if (u != 0) log_line("route line: %s", u > 0 ? msg : "update refused");
                    if (u > 0) {
                        u32 rrc = bl_reload_hosts();
                        log_line("hosts reload: %s (0x%x)", rrc == 0 ? "ok" : "failed", rrc);
                    }
                }
            }
        }

        if (!open) {
            if (now >= next_try) {
                int orc = relay_open(&r, "0.0.0.0", ROUTE_LOCAL_PORT, &remote, log_line);
                if (orc == 0) {
                    open = 1;
                    fails = 0;
                    if (own_ip[0]) inet_pton(AF_INET, own_ip, &r.own_ip);
                    log_line("listening on port %u (this console only) -> %s:%u", ROUTE_LOCAL_PORT,
                             inet_ntoa(remote.sin_addr), cfg.port);
                } else {
                    if (fails++ % 12 == 0) log_line("bind port %u failed (errno %d), retrying", ROUTE_LOCAL_PORT, -orc);
                    next_try = now + 5000;
                }
            }
            svcSleepThread(500000000ULL);
            continue;
        }

        int step = relay_step(&r, 1000, now);
        if (step < 0) {
            log_line("listening socket broke (errno %d): reopening", errno);
            relay_close(&r);
            open = 0;
            next_try = now + 2000;
            continue;
        }
        // Never let socket errors turn into a busy loop on the system core.
        window_errors += step;
        if (now - window_start >= 1000) {
            window_start = now;
            window_errors = 0;
        } else if (window_errors > ERROR_BUDGET_PER_S) {
            log_line("%d socket errors within a second: backing off", window_errors);
            svcSleepThread(500000000ULL);
            window_errors = 0;
            window_start = now_ms();
        }
    }
}
