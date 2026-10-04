// SPDX-License-Identifier: GPL-2.0-or-later
// Text on the Switch: the system's shared font through SDL2_ttf, with a small cache
// of rendered strings so redraws don't re-render every label.
#ifdef __SWITCH__
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <switch.h>

#include "gfx.h"

static const int k_px[FONT_COUNT] = {19, 23, 28, 40};
static TTF_Font *g_font[FONT_COUNT];
static SDL_Renderer *g_ren;

#define CACHE_SIZE 96
typedef struct {
    int font;
    gfx_color c;
    char s[128];
    SDL_Texture *tex;
    int w, h;
    unsigned used;
} cached;
static cached g_cache[CACHE_SIZE];
static unsigned g_clock;

bool text_init(SDL_Renderer *r) {
    g_ren = r;
    if (TTF_Init() != 0) return false;
    PlFontData font;
    if (R_FAILED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) return false;
    for (int i = 0; i < FONT_COUNT; i++) {
        g_font[i] = TTF_OpenFontRW(SDL_RWFromMem(font.address, font.size), 1, k_px[i]);
        if (!g_font[i]) return false;
    }
    return true;
}

void text_exit(void) {
    for (int i = 0; i < CACHE_SIZE; i++)
        if (g_cache[i].tex) SDL_DestroyTexture(g_cache[i].tex);
    for (int i = 0; i < FONT_COUNT; i++)
        if (g_font[i]) TTF_CloseFont(g_font[i]);
    TTF_Quit();
}

static cached *lookup(int font, gfx_color c, const char *s) {
    cached *oldest = &g_cache[0];
    for (int i = 0; i < CACHE_SIZE; i++) {
        cached *e = &g_cache[i];
        if (e->tex && e->font == font && !memcmp(&e->c, &c, sizeof c) && !strcmp(e->s, s)) {
            e->used = ++g_clock;
            return e;
        }
        if (e->used < oldest->used) oldest = e;
    }
    if (strlen(s) >= sizeof oldest->s) return NULL;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_font[font], s, (SDL_Color){c.r, c.g, c.b, c.a});
    if (!surf) return NULL;
    if (oldest->tex) SDL_DestroyTexture(oldest->tex);
    oldest->tex = SDL_CreateTextureFromSurface(g_ren, surf);
    oldest->w = surf->w;
    oldest->h = surf->h;
    SDL_FreeSurface(surf);
    oldest->font = font;
    oldest->c = c;
    strcpy(oldest->s, s);
    oldest->used = ++g_clock;
    return oldest->tex ? oldest : NULL;
}

int text_draw(int font, int x, int y, gfx_color c, const char *s) {
    cached *e = lookup(font, c, s);
    if (!e) return 0;
    SDL_Rect dst = {x, y, e->w, e->h};
    SDL_RenderCopy(g_ren, e->tex, NULL, &dst);
    return e->w;
}

int text_width(int font, const char *s) {
    int w = 0, h = 0;
    TTF_SizeUTF8(g_font[font], s, &w, &h);
    return w;
}

int text_height(int font) {
    return TTF_FontHeight(g_font[font]);
}
#endif
