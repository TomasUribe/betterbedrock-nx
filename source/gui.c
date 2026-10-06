// SPDX-License-Identifier: GPL-2.0-or-later
// BetterBedrock NX GUI: two pages switched with L/R - Online (Microsoft sign-in, your
// server, joining from Minecraft) and Graphics (Vibrant Visuals and its profiles) -
// plus a details page and a confirmation dialog. Buttons and touch. All behaviour
// lives in app.c; this file only draws and dispatches.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <switch.h>

#include "app.h"
#include "featured.h"
#include "gfx.h"

static const gfx_color BG_TOP = {20, 25, 35, 255}, BG_BOTTOM = {9, 11, 17, 255};
static const gfx_color CARD = {23, 28, 39, 255}, CARD_EDGE = {36, 43, 58, 255};
static const gfx_color FOCUS = {35, 44, 61, 255}, CHIP = {30, 37, 50, 255};
static const gfx_color TEXT = {236, 240, 245, 255}, MUTED = {148, 160, 178, 255}, DIM = {96, 107, 126, 255};
static const gfx_color GREEN = {74, 222, 128, 255}, GREEN_DARK = {21, 128, 61, 255}, GREEN_BG = {14, 42, 28, 255};
static const gfx_color RED = {248, 113, 113, 255}, YELLOW = {250, 204, 21, 255}, SHADE = {0, 0, 0, 170};

typedef enum {
    I_MS, I_NAME, I_ADDRESS, I_PORT, I_TEST, I_FEATURED, I_VIA, I_BC, I_ROUTING,  // Online
    I_VV, I_PROFILE,                                                             // Graphics
    I_RESTART, I_COUNT
} item;
typedef enum { PAGE_MAIN, PAGE_GRAPHICS, PAGE_DETAILS } page;

static item g_focus = I_NAME;
static item g_focus_other = I_VV;  // the other page's focus, kept while away
static page g_page = PAGE_MAIN;
static bool g_confirm_undo;
static bool g_testing;
static SDL_Rect g_hit[I_COUNT];
static SDL_Rect g_hit_undo, g_hit_cancel, g_hit_tab[2];

// ---- small widgets ----

static void card(int x, int y, int w, int h, const char *title) {
    gfx_round(x, y, w, h, 18, CARD_EDGE);
    gfx_round(x + 1, y + 1, w - 2, h - 2, 17, CARD);
    if (title) gfx_text(FONT_L, x + 24, y + 16, TEXT, title);
}

static bool is_focused(item i) {
    return g_focus == i && g_page != PAGE_DETAILS && !g_confirm_undo;
}

static void focus_bg(item i, int x, int y, int w, int h) {
    g_hit[i] = (SDL_Rect){x, y, w, h};
    if (!is_focused(i)) return;
    gfx_round(x, y, w, h, 12, FOCUS);
    gfx_round(x, y + 8, 4, h - 16, 2, GREEN);
}

static void button_glyph(int cx, int cy, const char *label, gfx_color bg, gfx_color fg) {
    gfx_circle(cx, cy, 14, bg);
    if (!strcmp(label, "<>")) {  // left/right: two small arrows, no text
        gfx_triangle(cx - 10, cy, cx - 3, cy - 6, cx - 3, cy + 6, fg);
        gfx_triangle(cx + 10, cy, cx + 3, cy - 6, cx + 3, cy + 6, fg);
        return;
    }
    gfx_text_fit(FONT_S, cx, cy - gfx_text_h(FONT_S) / 2, 28, 2, fg, label);
}

// A button; `primary` fills it green. Returns nothing; registers its hit area.
static void button(item i, int x, int y, int w, int h, const char *label, bool primary, bool enabled) {
    g_hit[i] = (SDL_Rect){x, y, w, h};
    bool focused = is_focused(i);
    gfx_color bg = !enabled ? CHIP : primary ? GREEN_DARK : CHIP;
    if (focused) gfx_round(x - 4, y - 4, w + 8, h + 8, 14, GREEN);
    gfx_round(x, y, w, h, 11, bg);
    gfx_text_fit(FONT_M, x + w / 2, y + (h - gfx_text_h(FONT_M)) / 2, w - 20, 2, enabled ? TEXT : DIM, label);
}

static void arrows(int x0, int x1, int cy, bool focused) {
    gfx_color c = focused ? GREEN : DIM;
    gfx_triangle(x0, cy, x0 + 12, cy - 10, x0 + 12, cy + 10, c);
    gfx_triangle(x1, cy, x1 - 12, cy - 10, x1 - 12, cy + 10, c);
}

static void toggle(int x, int y, bool on, bool focused) {
    if (focused) gfx_round(x - 4, y - 4, 96, 52, 26, GREEN);
    gfx_round(x, y, 88, 44, 22, on ? GREEN_DARK : CHIP);
    gfx_circle(on ? x + 66 : x + 22, y + 22, 17, on ? TEXT : MUTED);
}

static int chip(int right, int y, const char *text, gfx_color fg) {
    int w = gfx_text_w(FONT_S, text) + 28;
    gfx_round(right - w, y, w, 32, 16, CHIP);
    gfx_text(FONT_S, right - w + 14, y + (32 - gfx_text_h(FONT_S)) / 2, fg, text);
    return right - w - 10;
}

static gfx_color test_color(const test_result *t) {
    return t->kind == TEST_OK ? GREEN : t->kind == TEST_FAIL ? RED : MUTED;
}

// ---- screens ----

static void draw_header(void) {
    gfx_gradient(0, 0, GFX_W, GFX_H, BG_TOP, BG_BOTTOM);
    gfx_logo(40, 18, 72);
    gfx_text(FONT_XL, 128, 20, TEXT, "BetterBedrock NX");
    // page tabs: L / R
    static const char *const tabs[2] = {"Online", "Graphics"};
    int tx = 130;
    for (int t = 0; t < 2; t++) {
        bool active = (t == 0) == (g_page == PAGE_MAIN) && g_page != PAGE_DETAILS;
        int tw = gfx_text_w(FONT_S, tabs[t]) + 62;
        g_hit_tab[t] = (SDL_Rect){tx, 66, tw, 32};
        gfx_round(tx, 66, tw, 32, 16, active ? GREEN_DARK : CHIP);
        button_glyph(t == 0 ? tx + 18 : tx + tw - 18, 82, t == 0 ? "L" : "R", active ? TEXT : DIM, active ? GREEN_DARK : CHIP);
        gfx_text(FONT_S, t == 0 ? tx + 40 : tx + 16, 66 + (32 - gfx_text_h(FONT_S)) / 2, active ? TEXT : MUTED, tabs[t]);
        tx += tw + 10;
    }
    int x = 1240;
    x = chip(x, 22, "v" APP_VERSION_STR, DIM);
    x = chip(x, 22, app_active_file(), MUTED);
    x = chip(x, 22, app_dns_mitm() ? "dns.mitm on" : "dns.mitm OFF", app_dns_mitm() ? MUTED : RED);
    chip(x, 22, app_boot() == 1 ? "emuMMC" : app_boot() == 0 ? "sysMMC" : "boot unknown", MUTED);
}

static void draw_signin(int x, int y, int w, int h) {
    card(x, y, w, h, "Microsoft sign-in");
    ms_fix_state ms = app_ms_state();
    gfx_color dot = ms == MS_OFF ? RED : GREEN;
    const char *line = ms == MS_OFF ? "Broken: Minecraft's sign-in goes to Nextendo"
                       : ms == MS_ON ? "Fixed: Minecraft signs in with Microsoft"
                                     : "Working: nothing redirects Microsoft";
    const char *desc = ms == MS_OFF ? "The fix comments out only Nextendo's Microsoft lines."
                       : ms == MS_ON ? "Only Nextendo's Microsoft lines are commented out."
                                     : "Your hosts files don't send Microsoft anywhere else.";
    gfx_circle(x + 32, y + 78, 7, dot);
    gfx_text_fit(FONT_M, x + 50, y + 64, w - 74, 0, TEXT, line);
    gfx_text_fit(FONT_S, x + 24, y + 98, w - 48, 0, MUTED, desc);
    if (ms == MS_OFF) button(I_MS, x + 24, y + h - 60, 220, 42, "Fix sign-in", true, true);
    else if (ms == MS_ON) button(I_MS, x + 24, y + h - 60, 220, 42, "Undo fix", false, true);
    else button(I_MS, x + 24, y + h - 60, 220, 42, "Nothing to fix", false, false);
}

static void value_row(item i, int x, int y, int w, const char *label, const char *value, bool placeholder) {
    focus_bg(i, x + 12, y, w - 24, 46);
    gfx_text(FONT_M, x + 28, y + (46 - gfx_text_h(FONT_M)) / 2, MUTED, label);
    gfx_text_fit(FONT_M, x + w - 30, y + (46 - gfx_text_h(FONT_M)) / 2, w - 240, 1, placeholder ? DIM : TEXT, value);
}

static void draw_server(int x, int y, int w, int h) {
    card(x, y, w, h, "Your server");
    const route_config *c = app_cfg();
    char port[16];
    snprintf(port, sizeof port, "%u", c->port);
    value_row(I_NAME, x, y + 58, w, "Name", c->name[0] ? c->name : "Not set", !c->name[0]);
    value_row(I_ADDRESS, x, y + 108, w, "Address", c->address[0] ? c->address : "Not set", !c->address[0]);
    value_row(I_PORT, x, y + 158, w, "Port", port, false);
    button(I_TEST, x + 24, y + 218, 230, 42, g_testing ? "Testing..." : "Test connection", false, true);
    const test_result *t = app_test_server(), *b = app_test_bc();
    if (t->kind == TEST_NONE)
        gfx_text_fit(FONT_S, x + 24, y + 272, w - 48, 0, DIM, "Checks that your server answers, from this console.");
    else
        gfx_text_fit(FONT_S, x + 24, y + 272, w - 48, 0, test_color(t), t->text);
    if (b->kind != TEST_NONE) {
        char line[160];
        snprintf(line, sizeof line, "BedrockConnect: %s", b->text);
        gfx_text_fit(FONT_S, x + 24, y + 296, w - 48, 0, test_color(b), line);
    }
}

static void selector_row(item i, int x, int y, int w, const char *label, const char *value, const char *sub) {
    focus_bg(i, x + 12, y, w - 24, 62);
    bool focused = is_focused(i);
    gfx_text(FONT_M, x + 28, y + (62 - gfx_text_h(FONT_M)) / 2, MUTED, label);
    int x0 = x + w - 300, x1 = x + w - 34, mid = (x0 + x1) / 2;
    arrows(x0, x1, y + (sub ? 22 : 31), focused);
    gfx_text_fit(FONT_M, mid, y + (sub ? 8 : (62 - gfx_text_h(FONT_M)) / 2), x1 - x0 - 40, 2, TEXT, value);
    if (sub) gfx_text_fit(FONT_S, mid, y + 34, x1 - x0 - 20, 2, DIM, sub);
}

static void draw_join(int x, int y, int w, int h) {
    card(x, y, w, h, "Join from Minecraft");
    const route_config *c = app_cfg();
    const char *featured = app_featured_label();
    char path[160];
    snprintf(path, sizeof path, "Play > Servers > %s", featured);
    gfx_text_fit(FONT_S, x + 24, y + 54, w - 48, 0, MUTED, path);

    selector_row(I_FEATURED, x, y + 84, w, "Featured server", featured, c->replaces);
    bool bc = c->via == ROUTE_VIA_BEDROCKCONNECT;
    selector_row(I_VIA, x, y + 152, w, "Route via", bc ? "BedrockConnect" : "Direct", NULL);
    if (bc) {
        value_row(I_BC, x, y + 220, w, "BedrockConnect", c->bedrockconnect, false);
    } else {
        g_hit[I_BC] = (SDL_Rect){0, 0, 0, 0};
        char note[160];
        if (app_direct_needs_19132())
            snprintf(note, sizeof note, "Needs your server on port 19132 (it uses %u)", c->port);
        else
            snprintf(note, sizeof note, "Straight to %s, port 19132", c->address[0] ? c->address : "your server");
        gfx_text_fit(FONT_S, x + 28, y + 234, w - 56, 0, app_direct_needs_19132() ? RED : MUTED, note);
    }

    routing_state rs = app_routing();
    bool on = rs == ROUTING_ON || rs == ROUTING_LOADING || rs == ROUTING_STALE;
    focus_bg(I_ROUTING, x + 12, y + 280, w - 24, 72);
    gfx_text(FONT_L, x + 28, y + 290, TEXT, "Routing");
    const char *state = rs == ROUTING_ON ? "On - in effect now"
                        : rs == ROUTING_OFF ? "Off"
                        : rs == ROUTING_STALE ? "On for older settings - switch off and on"
                        : rs == ROUTING_LOADING ? "On - restart the console to load it"
                                                : "Off - still active until a restart";
    gfx_color sc = rs == ROUTING_ON ? GREEN : rs == ROUTING_OFF ? MUTED : YELLOW;
    gfx_text_fit(FONT_S, x + 28, y + 324, w - 180, 0, sc, state);
    toggle(x + w - 124, y + 294, on, is_focused(I_ROUTING));

    int by = y + 370, bh = h - (by - y) - 20;
    gfx_round(x + 20, by, w - 40, bh, 14, rs == ROUTING_ON ? GREEN_DARK : CARD_EDGE);
    gfx_round(x + 22, by + 2, w - 44, bh - 4, 12, rs == ROUTING_ON ? GREEN_BG : CARD);
    gfx_text(FONT_S, x + 40, by + 12, MUTED, "In Minecraft");
    if (rs != ROUTING_ON) {
        gfx_text_fit(FONT_M, x + 40, by + 40, w - 80, 0, TEXT, "Turn routing on, then open");
        gfx_text_fit(FONT_M, x + 40, by + 70, w - 80, 0, TEXT, path);
    } else if (bc) {
        char line[160];
        gfx_text_fit(FONT_M, x + 40, by + 38, w - 80, 0, TEXT, path);
        gfx_text_fit(FONT_M, x + 40, by + 66, w - 80, 0, TEXT, "Connect to a Server, then enter");
        snprintf(line, sizeof line, "%s   port %u", c->address[0] ? c->address : "your server's address", c->port);
        gfx_text_fit(FONT_M, x + 40, by + 94, w - 80, 0, GREEN, line);
    } else {
        gfx_text_fit(FONT_M, x + 40, by + 40, w - 80, 0, TEXT, path);
        gfx_text_fit(FONT_M, x + 40, by + 70, w - 80, 0, GREEN, "takes you straight to your server");
    }
}

// ---- Graphics page ----

static void draw_footer(bool details);

static void draw_vv(int x, int y, int w, int h) {
    card(x, y, w, h, "Vibrant Visuals");
    const gfx_state *g = app_gfx();
    const gfx_profile_def *d = gfx_profile_get(g->chosen);
    bool on = g->patch == 1;
    const char *line = on ? "On - for Minecraft " GFX_GAME_VERSION
                     : g->patch < 0 ? "Another file sits where the patch goes"
                                    : "Off - Minecraft's usual graphics";
    gfx_circle(x + 32, y + 70, 7, on ? GREEN : g->patch < 0 ? YELLOW : DIM);
    gfx_text_fit(FONT_M, x + 50, y + 56, w - 74, 0, TEXT, line);
    gfx_text_fit(FONT_S, x + 24, y + 88, w - 48, 0, MUTED,
                 "Lighting, shadows, sky and water from the newer consoles.");

    focus_bg(I_VV, x + 12, y + 122, w - 24, 72);
    gfx_text(FONT_L, x + 28, y + 132, TEXT, "Vibrant Visuals");
    gfx_text_fit(FONT_S, x + 28, y + 166, w - 180, 0, MUTED, "Applies the next time Minecraft starts");
    toggle(x + w - 124, y + 136, on, is_focused(I_VV));

    char sub[64];
    snprintf(sub, sizeof sub, "%s / %s docked", d->res_handheld, d->res_docked);
    selector_row(I_PROFILE, x, y + 206, w, "Profile", d->name, sub);

    char v[96];
    int ty = y + 284;
    snprintf(v, sizeof v, "%d x %d map, redrawn %s", d->shadow_resolution, d->shadow_resolution,
             d->shadow_every > 1 ? "every 2nd frame" : "every frame");
    gfx_text(FONT_S, x + 28, ty, MUTED, "Shadows");
    gfx_text_fit(FONT_S, x + 160, ty, w - 188, 0, TEXT, v);
    snprintf(v, sizeof v, "%d chunks with Vibrant Visuals", d->distance);
    gfx_text(FONT_S, x + 28, ty + 28, MUTED, "Distance");
    gfx_text_fit(FONT_S, x + 160, ty + 28, w - 188, 0, TEXT, v);
    snprintf(v, sizeof v, "bloom %s, clouds %s, %s", d->bloom ? "on" : "off", d->clouds,
             strcmp(d->reflections, "off") ? "light reflections and fog" : "no reflections or fog");
    gfx_text(FONT_S, x + 28, ty + 56, MUTED, "Effects");
    gfx_text_fit(FONT_S, x + 160, ty + 56, w - 188, 0, TEXT, v);

    int by = y + 378, bh = h - (by - y) - 20;
    gfx_round(x + 20, by, w - 40, bh, 14, on ? GREEN_DARK : CARD_EDGE);
    gfx_round(x + 22, by + 2, w - 44, bh - 4, 12, on ? GREEN_BG : CARD);
    gfx_text(FONT_S, x + 40, by + 12, MUTED, "In Minecraft");
    if (g->tuning == -2 && on) {
        gfx_text_fit(FONT_M, x + 40, by + 40, w - 80, 0, YELLOW, "The profile files were edited -");
        gfx_text_fit(FONT_M, x + 40, by + 70, w - 80, 0, YELLOW, "pick a profile to write them again");
    } else if (on) {
        gfx_text_fit(FONT_M, x + 40, by + 40, w - 80, 0, TEXT, "Settings > Video > Mode > Vibrant Visuals");
        gfx_text_fit(FONT_S, x + 40, by + 76, w - 80, 0, GREEN, "After a change: close Minecraft fully, then start it.");
    } else {
        gfx_text_fit(FONT_M, x + 40, by + 40, w - 80, 0, TEXT, "Turn Vibrant Visuals on, then start");
        gfx_text_fit(FONT_M, x + 40, by + 70, w - 80, 0, TEXT, "Minecraft and choose it under Video");
    }
}

static void tip(int x, int y, int w, const char *title, const char *text) {
    gfx_circle(x + 30, y + 13, 5, GREEN);
    gfx_text_fit(FONT_M, x + 46, y, w - 70, 0, TEXT, title);
    gfx_text_fit(FONT_S, x + 46, y + 30, w - 70, 0, MUTED, text);
}

static void draw_tips(int x, int y, int w, int h) {
    card(x, y, w, h, "Run it smoother");
    int ty = y + 64;
    tip(x, ty, w, "Overclock while you play", "Vibrant Visuals needs it on a Switch 1: raise the GPU");
    gfx_text_fit(FONT_S, x + 46, ty + 54, w - 70, 0, MUTED, "and memory clocks for Minecraft in your clock tool.");
    ty += 96;
    tip(x, ty, w, "Fast in handheld", "480p and the lightest shadows; Balanced suits docked.");
    ty += 72;
    tip(x, ty, w, "Keep graphics mode switching off", "Video > In-game graphics mode switching keeps both");
    gfx_text_fit(FONT_S, x + 46, ty + 54, w - 70, 0, MUTED, "renderers in memory while you play.");
    ty += 96;
    tip(x, ty, w, "Render distance 8 or less", "Video > Render Distance: fewer chunks to build.");
    ty += 72;
    tip(x, ty, w, "Steady frames", "A frame-rate limit (Video, or FPSLocker) evens out drops.");
}

static void draw_graphics(void) {
    draw_header();
    draw_vv(40, 116, 584, 516);
    draw_tips(656, 116, 584, 516);
    draw_footer(false);
}

static void draw_footer(bool details) {
    gfx_fill(40, 648, 1200, 1, CARD_EDGE);
    const char *msg;
    status_kind k = app_status(&msg);
    gfx_color mc = k == ST_OK ? GREEN : k == ST_ERROR ? RED : MUTED;
    int hints_x = 1240;
    // right-aligned button hints
    const char *labels[6];
    const char *keys[6];
    int n = 0;
    if (details) {
        keys[n] = "B", labels[n++] = "Back";
    } else if (g_confirm_undo) {
        keys[n] = "B", labels[n++] = "Cancel";
        keys[n] = "A", labels[n++] = "Undo";
    } else if (g_page == PAGE_GRAPHICS) {
        keys[n] = "+", labels[n++] = "Exit";
        keys[n] = "L", labels[n++] = "Online";
        if (g_focus == I_PROFILE) keys[n] = "<>", labels[n++] = "Change";
        keys[n] = "A", labels[n++] = "Select";
    } else {
        keys[n] = "+", labels[n++] = "Exit";
        keys[n] = "X", labels[n++] = "Details";
        keys[n] = "R", labels[n++] = "Graphics";
        if (g_focus == I_FEATURED || g_focus == I_VIA) keys[n] = "<>", labels[n++] = "Change";
        keys[n] = "A", labels[n++] = "Select";
    }
    for (int i = 0; i < n; i++) {
        int tw = gfx_text_w(FONT_M, labels[i]);
        hints_x -= tw;
        gfx_text(FONT_M, hints_x, 670, TEXT, labels[i]);
        hints_x -= 26;
        button_glyph(hints_x, 670 + gfx_text_h(FONT_M) / 2, keys[i], TEXT, CARD);
        hints_x -= 34;
    }
    int status_right = hints_x - 20;
    if (app_restart_needed() && !details) {
        button(I_RESTART, status_right - 200, 664, 200, 40, "Restart console", false, true);
        status_right -= 220;
    } else {
        g_hit[I_RESTART] = (SDL_Rect){0, 0, 0, 0};
    }
    if (msg[0]) gfx_text_fit(FONT_M, 40, 670, status_right - 40, 0, mc, msg);
}

static void draw_confirm(void) {
    gfx_fill(0, 0, GFX_W, GFX_H, SHADE);
    int w = 660, h = 250, x = (GFX_W - w) / 2, y = (GFX_H - h) / 2 - 20;
    card(x, y, w, h, "Undo the sign-in fix?");
    gfx_text_fit(FONT_M, x + 24, y + 70, w - 48, 0, TEXT, "Nextendo's Microsoft lines come back, so");
    gfx_text_fit(FONT_M, x + 24, y + 100, w - 48, 0, TEXT, "Minecraft's sign-in will fail again.");
    g_hit_cancel = (SDL_Rect){x + w - 404, y + h - 66, 180, 44};
    g_hit_undo = (SDL_Rect){x + w - 204, y + h - 66, 180, 44};
    gfx_round(g_hit_cancel.x, g_hit_cancel.y, 180, 44, 11, CHIP);
    gfx_text_fit(FONT_M, g_hit_cancel.x + 90, g_hit_cancel.y + 9, 160, 2, TEXT, "Cancel");
    gfx_round(g_hit_undo.x - 4, g_hit_undo.y - 4, 188, 52, 14, RED);
    gfx_round(g_hit_undo.x, g_hit_undo.y, 180, 44, 11, (gfx_color){127, 29, 29, 255});
    gfx_text_fit(FONT_M, g_hit_undo.x + 90, g_hit_undo.y + 9, 160, 2, TEXT, "Undo fix");
}

static void draw_details(void) {
    draw_header();
    card(40, 116, 1200, 516, "Details");
    char ip[64];
    int y = 172;
    gfx_text(FONT_M, 64, y, MUTED, "Microsoft sign-in");
    y += 36;
    for (int i = 0; i < app_ms_probe_count(); i++, y += 30) {
        bool red;
        const char *label = app_ms_probe(i, &red, ip, sizeof ip);
        char v[96];
        snprintf(v, sizeof v, red ? "redirected to %s" : "real Microsoft", ip);
        gfx_text_fit(FONT_S, 64, y, 260, 0, TEXT, label);
        gfx_text_fit(FONT_S, 330, y, 270, 0, red ? RED : GREEN, v);
    }
    y += 16;
    gfx_text(FONT_M, 64, y, MUTED, "Hosts files");
    y += 36;
    for (int i = 0; i < app_hosts_file_count(); i++) {
        char name[48];
        bool active;
        he_stats st;
        if (!app_hosts_file(i, name, sizeof name, &active, &st)) continue;
        char v[120];
        int n = snprintf(v, sizeof v, "MS %d on, %d off - Nintendo %d", st.ms_active, st.ms_disabled, st.nintendo_active);
        if (st.catchall + st.mixed) snprintf(v + n, sizeof v - n, " - %d left alone", st.catchall + st.mixed);
        gfx_text_fit(FONT_S, 64, y, 230, 0, active ? GREEN : TEXT, name);
        gfx_text_fit(FONT_S, 300, y, 330, 0, MUTED, v);
        y += 30;
    }
    gfx_text_fit(FONT_S, 64, 590, 560, 0, DIM, "Green file: the one Atmosphere reads on this boot.");

    y = 172;
    gfx_text(FONT_M, 660, y, MUTED, "Nintendo (never edited)");
    y += 36;
    for (int i = 0; i < app_nintendo_probe_count(); i++, y += 30) {
        nintendo_route how;
        const char *label = app_nintendo_probe(i, &how, ip, sizeof ip);
        char v[96];
        if (how == NIN_REDIRECTED) snprintf(v, sizeof v, "redirected to %s", ip);
        else if (how == NIN_BLOCKED) snprintf(v, sizeof v, "blocked (%s)", ip);
        else snprintf(v, sizeof v, "your DNS decides (90DNS blocks it)");
        gfx_text_fit(FONT_S, 660, y, 240, 0, TEXT, label);
        gfx_text_fit(FONT_S, 900, y, 320, 0, how == NIN_YOUR_DNS ? YELLOW : GREEN, v);
    }
    draw_footer(true);
}

static void draw_main(void) {
    draw_header();
    draw_signin(40, 116, 584, 182);
    draw_server(40, 314, 584, 318);
    draw_join(656, 116, 584, 516);
    draw_footer(false);
    if (g_confirm_undo) draw_confirm();
}

static void draw(void) {
    if (g_page == PAGE_DETAILS) draw_details();
    else if (g_page == PAGE_GRAPHICS) draw_graphics();
    else draw_main();
    gfx_present();
}

// ---- input ----

static bool visible(item i) {
    if (i == I_RESTART) return app_restart_needed();
    if ((i == I_VV || i == I_PROFILE) != (g_page == PAGE_GRAPHICS)) return false;
    if (i == I_BC) return app_cfg()->via == ROUTE_VIA_BEDROCKCONNECT;
    return true;
}

static void move_focus(int dir) {
    item i = g_focus;
    do i = (item)((i + dir + I_COUNT) % I_COUNT);
    while (!visible(i));
    g_focus = i;
}

static void set_page(page p) {
    if (p == g_page || p == PAGE_DETAILS || g_page == PAGE_DETAILS) {
        g_page = p;
        return;
    }
    item keep = g_focus;
    g_page = p;
    g_focus = g_focus_other;
    g_focus_other = keep;
    if (!visible(g_focus)) move_focus(1);
}

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    char *b = s;
    while (isspace((unsigned char)*b)) b++;
    if (b != s) memmove(s, b, strlen(b) + 1);
}

static bool keyboard(const char *guide, const char *initial, bool numbers, char *out, size_t cap) {
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
        app_set_status(ST_ERROR, "Keyboard unavailable (0x%x)", rc);
        return false;
    }
    swkbdConfigMakePresetDefault(&kbd);
    if (numbers) swkbdConfigSetType(&kbd, SwkbdType_NumPad);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, initial);
    swkbdConfigSetStringLenMax(&kbd, (u32)(cap - 1));
    rc = swkbdShow(&kbd, out, cap);
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;  // cancelled
    trim(out);
    return true;
}

static void activate(item i, int dir) {
    const route_config *c = app_cfg();
    char v[128], cur[16];
    switch (i) {
        case I_MS:
            if (app_ms_state() == MS_OFF) app_ms_fix(true);
            else if (app_ms_state() == MS_ON) g_confirm_undo = true;
            else app_set_status(ST_INFO, "Nothing to fix: nothing redirects Microsoft.");
            break;
        case I_NAME:
            if (keyboard("Server name (shown here and in the overlay)", c->name, false, v, sizeof c->name)) app_set_name(v);
            break;
        case I_ADDRESS:
            if (keyboard("Server address: IP or host name, without the port", c->address, false, v, sizeof c->address))
                app_set_address(v);
            break;
        case I_PORT:
            snprintf(cur, sizeof cur, "%u", c->port);
            if (keyboard("Server port (Bedrock default: 19132)", cur, true, v, 8)) app_set_port_text(v);
            break;
        case I_TEST:
            g_testing = true;
            draw();
            app_test();
            g_testing = false;
            break;
        case I_FEATURED:
            if (dir) app_cycle_featured(dir);
            else if (keyboard("Featured server host to replace (e.g. play.galaxite.net)", c->replaces, false, v,
                              sizeof c->replaces))
                app_set_featured_host(v);
            break;
        case I_VIA:
            app_toggle_via();
            if (!visible(g_focus)) move_focus(1);
            break;
        case I_BC:
            if (keyboard("BedrockConnect server (public one: " ROUTE_PUBLIC_BEDROCKCONNECT ")", c->bedrockconnect,
                         false, v, sizeof c->bedrockconnect))
                app_set_bedrockconnect(v);
            break;
        case I_ROUTING:
            app_routing_toggle();
            break;
        case I_VV:
            app_gfx_toggle();
            break;
        case I_PROFILE:
            app_gfx_cycle_profile(dir ? dir : 1);
            break;
        case I_RESTART:
            app_restart();
            break;
        default:
            break;
    }
    if (!visible(g_focus)) move_focus(-1);
}

static void on_buttons(u64 down) {
    if (g_confirm_undo) {
        if (down & HidNpadButton_A) {
            g_confirm_undo = false;
            app_ms_fix(false);
        } else if (down & HidNpadButton_B) {
            g_confirm_undo = false;
        }
        return;
    }
    if (g_page == PAGE_DETAILS) {
        if (down & (HidNpadButton_B | HidNpadButton_X)) set_page(PAGE_MAIN);
        return;
    }
    if (down & (HidNpadButton_L | HidNpadButton_ZL)) set_page(PAGE_MAIN);
    else if (down & (HidNpadButton_R | HidNpadButton_ZR)) set_page(PAGE_GRAPHICS);
    else if ((down & HidNpadButton_X) && g_page == PAGE_MAIN) set_page(PAGE_DETAILS);
    else if (down & HidNpadButton_AnyUp) move_focus(-1);
    else if (down & HidNpadButton_AnyDown) move_focus(1);
    else if ((down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) &&
             (g_focus == I_FEATURED || g_focus == I_VIA || g_focus == I_PROFILE))
        activate(g_focus, (down & HidNpadButton_AnyLeft) ? -1 : 1);
    else if (down & HidNpadButton_A) activate(g_focus, 0);
}

static bool inside(SDL_Rect r, int x, int y) {
    return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void on_touch(int x, int y) {
    if (g_confirm_undo) {
        if (inside(g_hit_undo, x, y)) {
            g_confirm_undo = false;
            app_ms_fix(false);
        } else if (inside(g_hit_cancel, x, y)) {
            g_confirm_undo = false;
        }
        return;
    }
    if (g_page == PAGE_DETAILS) {
        set_page(PAGE_MAIN);
        return;
    }
    for (int t = 0; t < 2; t++) {
        if (inside(g_hit_tab[t], x, y)) {
            set_page(t == 0 ? PAGE_MAIN : PAGE_GRAPHICS);
            return;
        }
    }
    for (int i = 0; i < I_COUNT; i++) {
        if (!visible((item)i) || !inside(g_hit[i], x, y)) continue;
        g_focus = (item)i;
        if (i == I_FEATURED || i == I_VIA || i == I_PROFILE) {
            // the arrows sit in the right part of the row
            SDL_Rect r = g_hit[i];
            int x0 = r.x + r.w - 288, mid = x0 + 134;
            if (x >= x0) activate((item)i, x < mid ? -1 : 1);
        } else {
            activate((item)i, 0);
        }
        return;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    romfsInit();
    plInitialize(PlServiceType_User);
    bool sockets = R_SUCCEEDED(socketInitializeDefault());
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    app_init(sockets);
    if (app_ms_state() == MS_OFF) g_focus = I_MS;
    if (!gfx_init()) {
        app_exit();
        return 1;
    }
#ifndef __SWITCH__
    const char *shots = getenv("BL_SHOTS");
    int shot = 0;
    char path[256];
#endif
    draw();
#ifndef __SWITCH__
    if (shots) {
        snprintf(path, sizeof path, "%s/shot_%02d.bmp", shots, shot++);
        gfx_save(path);
    }
#endif
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_FINGERDOWN) on_touch((int)(e.tfinger.x * GFX_W), (int)(e.tfinger.y * GFX_H));
        if (down & HidNpadButton_Plus) break;
        if (down) on_buttons(down);
        draw();
#ifndef __SWITCH__
        if (shots) {
            snprintf(path, sizeof path, "%s/shot_%02d.bmp", shots, shot++);
            gfx_save(path);
        }
#endif
    }
    gfx_exit();
    app_exit();
    if (sockets) socketExit();
    plExit();
    romfsExit();
    return 0;
}
