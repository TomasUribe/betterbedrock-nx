// SPDX-License-Identifier: GPL-2.0-or-later
// BedrockLink drawing layer on SDL2. See gfx.h.
#include "gfx.h"

#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Surface *g_target;  // PC preview: offscreen surface
static SDL_Texture *g_logo;

#ifdef __SWITCH__
#define LOGO_PATH "romfs:/logo.bmp"
#else
#define LOGO_PATH "romfs/logo.bmp"
#endif

bool gfx_init(void) {
#ifdef __SWITCH__
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
    g_win = SDL_CreateWindow("BedrockLink", 0, 0, GFX_W, GFX_H, 0);
    if (!g_win) return false;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#else
    if (SDL_Init(0) != 0) return false;
    g_target = SDL_CreateRGBSurfaceWithFormat(0, GFX_W, GFX_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!g_target) return false;
    g_ren = SDL_CreateSoftwareRenderer(g_target);
#endif
    if (!g_ren) return false;
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    SDL_Surface *logo = SDL_LoadBMP(LOGO_PATH);
    if (logo) {
        g_logo = SDL_CreateTextureFromSurface(g_ren, logo);
        SDL_FreeSurface(logo);
    }
    return text_init(g_ren);
}

void gfx_exit(void) {
    text_exit();
    if (g_logo) SDL_DestroyTexture(g_logo);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    if (g_target) SDL_FreeSurface(g_target);
    SDL_Quit();
}

void gfx_present(void) {
    SDL_RenderPresent(g_ren);
}

bool gfx_save(const char *path) {
    return g_target && SDL_SaveBMP(g_target, path) == 0;
}

static void set(gfx_color c) {
    SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);
}

void gfx_fill(int x, int y, int w, int h, gfx_color c) {
    if (w <= 0 || h <= 0) return;
    set(c);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(g_ren, &r);
}

void gfx_gradient(int x, int y, int w, int h, gfx_color top, gfx_color bottom) {
    for (int i = 0; i < h; i++) {
        float t = h > 1 ? (float)i / (float)(h - 1) : 0.0f;
        gfx_color c = {(unsigned char)(top.r + (bottom.r - top.r) * t), (unsigned char)(top.g + (bottom.g - top.g) * t),
                       (unsigned char)(top.b + (bottom.b - top.b) * t), (unsigned char)(top.a + (bottom.a - top.a) * t)};
        set(c);
        SDL_RenderDrawLine(g_ren, x, y + i, x + w - 1, y + i);
    }
}

// Half-width of a circle of radius r at a distance dy from its centre row.
static int span(int r, int dy) {
    int x = 0;
    while ((x + 1) * (x + 1) + dy * dy <= r * r) x++;
    return x;
}

void gfx_round(int x, int y, int w, int h, int r, gfx_color c) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    set(c);
    for (int i = 0; i < h; i++) {
        int inset = 0;
        if (i < r) inset = r - span(r, r - i);
        else if (i >= h - r) inset = r - span(r, i - (h - r) + 1);
        SDL_RenderDrawLine(g_ren, x + inset, y + i, x + w - 1 - inset, y + i);
    }
}

void gfx_round_outline(int x, int y, int w, int h, int r, int thick, gfx_color c) {
    // Outline = ring between the outer shape and an inset one, drawn row by row.
    if (r * 2 > h) r = h / 2;
    set(c);
    for (int i = 0; i < h; i++) {
        int outer = 0;
        if (i < r) outer = r - span(r, r - i);
        else if (i >= h - r) outer = r - span(r, i - (h - r) + 1);
        int ii = i - thick, ih = h - 2 * thick, ir = r - thick > 0 ? r - thick : 0;
        if (ii < 0 || ii >= ih) {
            SDL_RenderDrawLine(g_ren, x + outer, y + i, x + w - 1 - outer, y + i);
            continue;
        }
        int inner = 0;
        if (ii < ir) inner = ir - span(ir, ir - ii);
        else if (ii >= ih - ir) inner = ir - span(ir, ii - (ih - ir) + 1);
        SDL_RenderDrawLine(g_ren, x + outer, y + i, x + thick + inner - 1, y + i);
        SDL_RenderDrawLine(g_ren, x + w - thick - inner, y + i, x + w - 1 - outer, y + i);
    }
}

void gfx_circle(int cx, int cy, int r, gfx_color c) {
    set(c);
    for (int dy = -r; dy <= r; dy++) {
        int s = span(r, dy < 0 ? -dy : dy);
        SDL_RenderDrawLine(g_ren, cx - s, cy + dy, cx + s, cy + dy);
    }
}

void gfx_triangle(int x0, int y0, int x1, int y1, int x2, int y2, gfx_color c) {
    SDL_Color col = {c.r, c.g, c.b, c.a};
    SDL_Vertex v[3] = {{{(float)x0, (float)y0}, col, {0, 0}},
                       {{(float)x1, (float)y1}, col, {0, 0}},
                       {{(float)x2, (float)y2}, col, {0, 0}}};
    SDL_RenderGeometry(g_ren, NULL, v, 3, NULL, 0);
}

void gfx_logo(int x, int y, int size) {
    SDL_Rect dst = {x, y, size, size};
    if (g_logo) SDL_RenderCopy(g_ren, g_logo, NULL, &dst);
}

int gfx_text(int font, int x, int y, gfx_color c, const char *s) {
    return s && *s ? text_draw(font, x, y, c, s) : 0;
}

int gfx_text_w(int font, const char *s) {
    return s && *s ? text_width(font, s) : 0;
}

int gfx_text_h(int font) {
    return text_height(font);
}

int gfx_text_fit(int font, int x, int y, int max_w, int align, gfx_color c, const char *s) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", s ? s : "");
    if (text_width(font, buf) > max_w) {
        size_t n = strlen(buf);
        while (n > 0) {
            // drop whole UTF-8 characters
            do n--;
            while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80);
            snprintf(buf + n, sizeof buf - n, "...");
            if (text_width(font, buf) <= max_w) break;
        }
    }
    int w = text_width(font, buf);
    int dx = align == 1 ? x - w : align == 2 ? x - w / 2 : x;
    return gfx_text(font, dx, y, c, buf);
}
