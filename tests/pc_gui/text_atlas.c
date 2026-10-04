// SPDX-License-Identifier: GPL-2.0-or-later
// Text in the PC preview: glyphs from the atlas made by tools/make_font_atlas.py.
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "../../source/gfx.h"
#include "font_atlas.h"

static SDL_Renderer *g_ren;
static SDL_Texture *g_atlas;

bool text_init(SDL_Renderer *r) {
    g_ren = r;
    FILE *f = fopen(getenv("BL_ATLAS") ? getenv("BL_ATLAS") : "font_atlas.bin", "rb");
    if (!f) return false;
    unsigned char *alpha = malloc(ATLAS_W * ATLAS_H);
    size_t got = fread(alpha, 1, ATLAS_W * ATLAS_H, f);
    fclose(f);
    if (got != ATLAS_W * ATLAS_H) return false;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ATLAS_W, ATLAS_H, 32, SDL_PIXELFORMAT_ARGB8888);
    for (int i = 0; i < ATLAS_W * ATLAS_H; i++) ((Uint32 *)s->pixels)[i] = ((Uint32)alpha[i] << 24) | 0xFFFFFF;
    free(alpha);
    g_atlas = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(g_atlas, SDL_BLENDMODE_BLEND);
    return g_atlas != NULL;
}

void text_exit(void) {
    if (g_atlas) SDL_DestroyTexture(g_atlas);
}

// Next code point of a UTF-8 string; anything past Latin-1 becomes '?'.
static int next_cp(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    int cp = *s++, extra = 0;
    if (cp >= 0xF0) extra = 3, cp &= 0x07;
    else if (cp >= 0xE0) extra = 2, cp &= 0x0F;
    else if (cp >= 0xC0) extra = 1, cp &= 0x1F;
    while (extra-- > 0 && (*s & 0xC0) == 0x80) cp = (cp << 6) | (*s++ & 0x3F);
    *p = (const char *)s;
    return cp < 256 ? cp : '?';
}

int text_draw(int font, int x, int y, gfx_color c, const char *s) {
    SDL_SetTextureColorMod(g_atlas, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(g_atlas, c.a);
    int pen = x;
    while (*s) {
        const atlas_glyph *g = &atlas_glyphs[font][next_cp(&s)];
        if (g->w) {
            SDL_Rect src = {g->x, g->y, g->w, g->h}, dst = {pen + g->ox, y + g->oy, g->w, g->h};
            SDL_RenderCopy(g_ren, g_atlas, &src, &dst);
        }
        pen += g->adv;
    }
    return pen - x;
}

int text_width(int font, const char *s) {
    int w = 0;
    while (*s) w += atlas_glyphs[font][next_cp(&s)].adv;
    return w;
}

int text_height(int font) {
    return atlas_line_h[font];
}
