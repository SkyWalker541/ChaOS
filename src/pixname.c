/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS Pixel Names. See pixname.h. */
#include "pixname.h"
#include "pixfont.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PN_CARDS   32
static float g_ms = 950.0f;      /* to assemble, or to scatter: pn_speed */
#define PN_MS g_ms
#define PN_RADIUS  28
#define PN_BAR_Y   (PN_H * 80 / 100)

/* the logo's colours (tools/genlogo.py) and the classic cards' ground */
static const SDL_Color CREAM = { 0xF4, 0xEA, 0xD5, 255 };
static const SDL_Color CYAN  = { 0x39, 0xC5, 0xD6, 255 };
static const SDL_Color TOP   = { 34, 38, 51, 255 };
static const SDL_Color BOT   = { 25, 28, 38, 255 };

typedef struct {
	char         name[64];
	SDL_Texture *tex;            /* the one last handed back */
	float        t;              /* 0 scattered .. 1 assembled */
	float        drawn;          /* the t it was last drawn at, or -1 */
	Uint32       at;             /* when t was last moved */
	int          n, cols, rows;
	pf_pt        pts[PF_PTS_MAX];
	float        ang[PF_PTS_MAX], dist[PF_PTS_MAX], delay[PF_PTS_MAX];
} pn_state;

static pn_state *g_pn[PN_CARDS];
static SDL_Texture *g_ground;    /* the card's ground, made once */
static bool g_moving;
static float g_fix = -2.0f;      /* -2: animate; -1: ends at once; else that t */

void pn_fix(float t) { g_fix = t; }
void pn_speed(int ms) { if (ms > 50) g_ms = (float)ms; }
float pn_fixed(void) { return g_fix; }

static unsigned rng(unsigned *s) { *s = *s * 1103515245u + 12345u; return *s >> 8; }
static float frand(unsigned *s) { return (float)(rng(s) & 0xFFFF) / 65535.0f; }

static pn_state *state_for(const char *name)
{
	int i, free_at = -1;
	pn_state *p;
	unsigned seed = 2166136261u;

	for (i = 0; i < PN_CARDS; i++) {
		if (g_pn[i] && !strcmp(g_pn[i]->name, name)) return g_pn[i];
		if (!g_pn[i] && free_at < 0) free_at = i;
	}
	if (free_at < 0 || !(p = calloc(1, sizeof *p))) return NULL;
	snprintf(p->name, sizeof p->name, "%s", name);
	p->n = pf_layout(name, p->pts, PF_PTS_MAX, &p->cols, &p->rows);
	for (i = 0; name[i]; i++) seed = (seed ^ (unsigned char)name[i]) * 16777619u;
	for (i = 0; i < p->n; i++) {
		p->ang[i] = frand(&seed) * 6.2831853f;
		p->dist[i] = 120.0f + frand(&seed) * 400.0f;
		p->delay[i] = frand(&seed) * 0.35f;
	}
	p->drawn = -1.0f;
	p->at = SDL_GetTicks();
	g_pn[free_at] = p;
	return p;
}

/* The ground: the classic cards' dark gradient, rounded, drawn once */
static SDL_Texture *ground(SDL_Renderer *r)
{
	SDL_Surface *s;
	int x, y;

	if (g_ground) return g_ground;
	if (!(s = SDL_CreateRGBSurfaceWithFormat(0, PN_W, PN_H, 32, SDL_PIXELFORMAT_RGBA32)))
		return NULL;
	for (y = 0; y < PN_H; y++) {
		Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
		float k = (float)y / (PN_H - 1);
		Uint8 cr = (Uint8)(TOP.r + (BOT.r - TOP.r) * k), cg = (Uint8)(TOP.g + (BOT.g - TOP.g) * k),
		      cb = (Uint8)(TOP.b + (BOT.b - TOP.b) * k);
		for (x = 0; x < PN_W; x++) {
			/* a corner's quarter circle, with a soft edge */
			int cx = x < PN_RADIUS ? PN_RADIUS : x >= PN_W - PN_RADIUS ? PN_W - PN_RADIUS - 1 : x;
			int cy = y < PN_RADIUS ? PN_RADIUS : y >= PN_H - PN_RADIUS ? PN_H - PN_RADIUS - 1 : y;
			float d = sqrtf((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
			float a = d <= PN_RADIUS - 1 ? 1.0f : d >= PN_RADIUS ? 0.0f : PN_RADIUS - d;
			row[x] = SDL_MapRGBA(s->format, cr, cg, cb, (Uint8)(a * 255));
		}
	}
	g_ground = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	return g_ground;
}

static void square(SDL_Renderer *r, float x, float y, int cell, SDL_Color c, float a)
{
	SDL_Rect q = { (int)lroundf(x), (int)lroundf(y), cell, cell };
	if (a <= 0.0f) return;
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, (Uint8)(a > 1.0f ? 255 : a * 255));
	SDL_RenderFillRect(r, &q);
}

/* The name's pixels at t, the block's top left at ox,oy, the loose ones kept
 * inside w x h */
static void pixels(SDL_Renderer *r, const pn_state *p, SDL_Color acc, float t,
                   int ox, int oy, int cell, int w, int h, float dim)
{
	static const struct { int dx, dy, cyan; } SPARK[] = {
		{ 2, -2, 0 }, { 4, -3, 1 }, { 5, -1, 0 }, { 7, -4, 1 }, { 3, -5, 0 },
	};
	int i;

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	for (i = 0; i < p->n; i++) {
		float tx = (float)(ox + p->pts[i].x * cell), ty = (float)(oy + p->pts[i].y * cell);
		float sx = tx + cosf(p->ang[i]) * p->dist[i], sy = ty + sinf(p->ang[i]) * p->dist[i];
		float k = (t - p->delay[i]) / (1.0f - p->delay[i]), e;
		if (k < 0.0f) k = 0.0f;
		if (k > 1.0f) k = 1.0f;
		e = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
		/* kept inside: a loose pixel past the edge folds back in rather than
		 * being drawn over the next card */
		sx = sx < 8 ? 8 + fmodf(8 - sx, 40) : sx > w - 8 - cell ? w - 8 - cell - fmodf(sx, 40) : sx;
		sy = sy < 8 ? 8 + fmodf(8 - sy, 40) : sy > h - 8 - cell ? h - 8 - cell - fmodf(sy, 40) : sy;
		square(r, sx + (tx - sx) * e, sy + (ty - sy) * e, cell,
		       p->pts[i].accent ? acc : CREAM, (dim + (1.0f - dim) * e));
	}
	/* the logo's escaping pixels, top right of the name, once it has formed */
	if (t > 0.6f)
		for (i = 0; i < (int)(sizeof SPARK / sizeof SPARK[0]); i++)
			square(r, (float)(ox + (p->cols + SPARK[i].dx - 3) * cell),
			       (float)(oy + SPARK[i].dy * cell), cell,
			       SPARK[i].cyan ? CYAN : acc, (t - 0.6f) / 0.4f);
}

static void draw(SDL_Renderer *r, pn_state *p, SDL_Color acc)
{
	/* 53 columns at most (pixfont-check): 11 pixels each */
	const int cell = 11;
	int ox = (PN_W - p->cols * cell) / 2, oy = (PN_H * 42 / 100) - p->rows * cell / 2;
	SDL_Rect bar = { 48, PN_BAR_Y, PN_W - 96, 8 };

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
	SDL_RenderClear(r);
	if (ground(r)) {
		SDL_SetTextureBlendMode(g_ground, SDL_BLENDMODE_NONE);
		SDL_RenderCopy(r, g_ground, NULL, NULL);
	}
	pixels(r, p, acc, p->t, ox, oy, cell, PN_W, PN_H, 0.22f);
	SDL_SetRenderDrawColor(r, acc.r, acc.g, acc.b, 255);
	SDL_RenderFillRect(r, &bar);
}

void pn_overlay(SDL_Renderer *r, const char *name, unsigned accent, float t,
                int screen_w, int screen_h)
{
	pn_state *p = state_for(name ? name : "");
	SDL_Color acc = { (Uint8)(accent >> 16), (Uint8)(accent >> 8), (Uint8)accent, 255 };
	const int cell = 14;
	int ox, oy;
	SDL_Rect band;

	if (!p || t <= 0.0f) return;
	if (t > 1.0f) t = 1.0f;
	ox = (screen_w - p->cols * cell) / 2;
	oy = (screen_h - p->rows * cell) / 2;
	/* a dark band under the name, so it reads over any box art */
	band = (SDL_Rect){ 0, oy - cell * 7, screen_w, p->rows * cell + cell * 14 };
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, BOT.r, BOT.g, BOT.b, (Uint8)(200 * t));
	SDL_RenderFillRect(r, &band);
	/* loose pixels start unseen here: there is a game under them */
	pixels(r, p, acc, t, ox, oy, cell, screen_w, screen_h, 0.0f);
}

SDL_Texture *pn_card(SDL_Renderer *r, SDL_Texture *held, const char *name,
                     unsigned accent, bool focused)
{
	pn_state *p = state_for(name ? name : "");
	SDL_Color acc = { (Uint8)(accent >> 16), (Uint8)(accent >> 8), (Uint8)accent, 255 };
	Uint32 now = SDL_GetTicks();
	float step;

	if (!p) return held;
	/* What the caller holds: ours, or a card of ours from before (adopted), or
	 * something else - a picture loaded for another theme - put away */
	if (held != p->tex) {
		int acc_mode = 0, w = 0, h = 0;
		if (held && SDL_QueryTexture(held, NULL, &acc_mode, &w, &h) == 0 &&
		    acc_mode == SDL_TEXTUREACCESS_TARGET && w == PN_W && h == PN_H) {
			p->tex = held;
		} else {
			if (held) SDL_DestroyTexture(held);
			p->tex = NULL;
		}
		p->drawn = -1.0f;
	}
	if (!p->tex) {
		p->tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
		                           PN_W, PN_H);
		if (!p->tex) return NULL;
		SDL_SetTextureBlendMode(p->tex, SDL_BLENDMODE_BLEND);
		p->drawn = -1.0f;
	}
	/* toward its end, by the time since it last moved; a long gap (the shelf
	 * not drawn) is one step, not a jump */
	step = (float)(now - p->at) / PN_MS;
	if (step > 0.1f) step = 0.1f;
	p->at = now;
	if (focused && p->t < 1.0f) { p->t += step; if (p->t > 1.0f) p->t = 1.0f; }
	if (!focused && p->t > 0.0f) { p->t -= step; if (p->t < 0.0f) p->t = 0.0f; }
	if (g_fix == -1.0f) p->t = focused ? 1.0f : 0.0f;
	else if (g_fix >= 0.0f) p->t = focused ? g_fix : 1.0f - g_fix;
	if (p->t > 0.0f && p->t < 1.0f) g_moving = true;

	if (p->drawn != p->t) {
		SDL_Texture *was = SDL_GetRenderTarget(r);
		SDL_Rect vp, clip;
		SDL_bool clipped = SDL_RenderIsClipEnabled(r);

		SDL_RenderGetViewport(r, &vp);
		SDL_RenderGetClipRect(r, &clip);
		if (SDL_SetRenderTarget(r, p->tex) == 0) {
			draw(r, p, acc);
			p->drawn = p->t;
		}
		SDL_SetRenderTarget(r, was);
		SDL_RenderSetViewport(r, &vp);
		SDL_RenderSetClipRect(r, clipped ? &clip : NULL);
	}
	return p->tex;
}

bool pn_animating(void)
{
	bool m = g_moving;
	g_moving = false;
	return m;
}
