// SPDX-License-Identifier: GPL-2.0-or-later
// BetterBedrock NX drawing layer: SDL2 shapes, text and the logo on a 1280x720 screen.
// On the Switch text comes from the system font (SDL2_ttf); the PC preview build
// uses a pre-rendered font atlas instead (tests/pc_gui).
#ifndef BBNX_GFX_H
#define BBNX_GFX_H

#include <stdbool.h>

#define GFX_W 1280
#define GFX_H 720

typedef struct {
    unsigned char r, g, b, a;
} gfx_color;

enum { FONT_S, FONT_M, FONT_L, FONT_XL, FONT_COUNT };  // 19, 23, 28, 40 px

bool gfx_init(void);
void gfx_exit(void);
void gfx_present(void);
bool gfx_save(const char *path);  // PC preview only (BMP)

void gfx_gradient(int x, int y, int w, int h, gfx_color top, gfx_color bottom);
void gfx_fill(int x, int y, int w, int h, gfx_color c);
void gfx_round(int x, int y, int w, int h, int r, gfx_color c);
void gfx_round_outline(int x, int y, int w, int h, int r, int thick, gfx_color c);
void gfx_circle(int cx, int cy, int r, gfx_color c);
void gfx_triangle(int x0, int y0, int x1, int y1, int x2, int y2, gfx_color c);
void gfx_logo(int x, int y, int size);

int gfx_text(int font, int x, int y, gfx_color c, const char *s);  // y = top; returns width
int gfx_text_w(int font, const char *s);
int gfx_text_h(int font);
// Draws s cut with "..." to fit max_w; align: 0 left, 1 right (x = right edge), 2 centre.
int gfx_text_fit(int font, int x, int y, int max_w, int align, gfx_color c, const char *s);

// Text backends (text_ttf.c on the Switch, the atlas in the PC preview).
struct SDL_Renderer;
bool text_init(struct SDL_Renderer *r);
void text_exit(void);
int text_draw(int font, int x, int y, gfx_color c, const char *s);
int text_width(int font, const char *s);
int text_height(int font);

#endif
