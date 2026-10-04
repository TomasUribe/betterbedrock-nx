// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal libnx stand-in so the BedrockLink app runs on a PC against a fake "sdmc:" folder.
// Buttons come from $BL_KEYS, one per frame: A B X Y + U(p) D(own) L(eft) R(ight); anything else = none.
// Keyboard entries come from $BL_KBD, separated by '|'.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t Result;
typedef int SplConfigItem;
typedef struct { int unused; } PadState;
typedef struct { int type; } SwkbdConfig;
#define R_SUCCEEDED(r) ((r) == 0)
#define R_FAILED(r) ((r) != 0)
#define HidNpadStyleSet_NpadStandard 0
#define HidNpadButton_A (1u << 0)
#define HidNpadButton_B (1u << 1)
#define HidNpadButton_X (1u << 2)
#define HidNpadButton_Y (1u << 3)
#define HidNpadButton_Plus (1u << 10)
#define HidNpadButton_AnyLeft (1u << 12)
#define HidNpadButton_AnyUp (1u << 13)
#define HidNpadButton_AnyRight (1u << 14)
#define HidNpadButton_AnyDown (1u << 15)
#define SwkbdType_NumPad 1
static inline Result splInitialize(void) { return 0; }
static inline Result splGetConfig(SplConfigItem i, u64 *v) { (void)i; *v = 1; return 0; }
static inline void splExit(void) {}
static inline Result bpcInitialize(void) { puts("[stub] bpcInitialize"); return 0; }
static inline Result bpcRebootSystem(void) { puts("[stub] bpcRebootSystem"); return 0x1234; }
static inline void bpcExit(void) {}
static inline Result pmdmntInitialize(void) { return 0; }
static inline Result pmdmntGetProcessId(u64 *pid, u64 tid) { (void)tid; *pid = 0; return 0x20f; }
static inline void pmdmntExit(void) {}
static inline Result socketInitializeDefault(void) { return 0; }
static inline Result romfsInit(void) { return 0; }
static inline void romfsExit(void) {}
typedef enum { PlServiceType_User = 0 } PlServiceType;
static inline Result plInitialize(PlServiceType t) { (void)t; return 0; }
static inline void plExit(void) {}
static inline void socketExit(void) {}
static inline int fsdevCommitDevice(const char *n) { (void)n; return 0; }
static inline void consoleInit(void *p) { (void)p; }
static inline void consoleClear(void) { puts("\n================ screen ================"); }
static inline void consoleUpdate(void *p) { (void)p; }
static inline void consoleExit(void *p) { (void)p; }
static inline void padConfigureInput(int a, int b) { (void)a; (void)b; }
static inline void padInitializeDefault(PadState *p) { (void)p; }
static inline void padUpdate(PadState *p) { (void)p; }
static inline bool appletMainLoop(void) { return true; }
static inline u64 padGetButtonsDown(PadState *p) {
    (void)p;
    static const char *s;
    if (!s) s = getenv("BL_KEYS") ? getenv("BL_KEYS") : "+";
    char c = *s ? *s++ : '+';
    switch (c) {
        case 'A': return HidNpadButton_A;
        case 'B': return HidNpadButton_B;
        case 'X': return HidNpadButton_X;
        case 'Y': return HidNpadButton_Y;
        case 'U': return HidNpadButton_AnyUp;
        case 'D': return HidNpadButton_AnyDown;
        case 'L': return HidNpadButton_AnyLeft;
        case 'R': return HidNpadButton_AnyRight;
        case '+': return HidNpadButton_Plus;
        default: return 0;
    }
}
static inline Result swkbdCreate(SwkbdConfig *c, int n) { (void)n; c->type = 0; return 0; }
static inline void swkbdConfigMakePresetDefault(SwkbdConfig *c) { (void)c; }
static inline void swkbdConfigSetType(SwkbdConfig *c, int t) { c->type = t; }
static inline void swkbdConfigSetGuideText(SwkbdConfig *c, const char *t) { (void)c; printf("[stub] keyboard: %s\n", t); }
static inline void swkbdConfigSetInitialText(SwkbdConfig *c, const char *t) { (void)c; (void)t; }
static inline void swkbdConfigSetStringLenMax(SwkbdConfig *c, u32 n) { (void)c; (void)n; }
static inline void swkbdClose(SwkbdConfig *c) { (void)c; }
static inline Result swkbdShow(SwkbdConfig *c, char *out, size_t cap) {
    (void)c;
    static char buf[1024];
    static char *next;
    if (!next) {
        snprintf(buf, sizeof buf, "%s", getenv("BL_KBD") ? getenv("BL_KBD") : "");
        next = buf;
    }
    if (!*next) return 0x5d59;  // cancelled
    char *bar = strchr(next, '|');
    if (bar) *bar = '\0';
    snprintf(out, cap, "%s", next);
    next = bar ? bar + 1 : next + strlen(next);
    return 0;
}
