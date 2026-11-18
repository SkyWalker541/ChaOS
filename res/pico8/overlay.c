/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: an overlay over the player's own PICO-8 (src/pico8.h).
 *
 * PICO-8 is Lexaloffle's program, drawing its own screen through SDL, so
 * nothing in ChaOS's game engine is between it and the display. This is
 * loaded into it (LD_PRELOAD, res/pico8/run.sh) when the PICO-8 console has an
 * overlay chosen: each frame, just before PICO-8 shows it, the overlay is drawn
 * over the whole screen, its transparent window round the game. The launcher
 * leaves the overlay, made the screen's size, as a BMP SDL itself can read
 * (CHAOS_P8_OVERLAY), so this needs nothing but the SDL PICO-8 already has.
 *
 * Both of SDL's ways to show a frame are covered: a renderer
 * (SDL_RenderPresent), and a window surface (SDL_UpdateWindowSurface).
 * Nothing of PICO-8's drawing state is left changed: the renderer's logical
 * size, viewport, clip, scale and target are put back after. Built for
 * the Brick by mk/cross.mk; never part of the launcher. */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

static SDL_Surface  *g_img;
static SDL_Texture  *g_tex;
static SDL_Renderer *g_tex_for;
static int g_tried;

static SDL_Surface *overlay(void)
{
	const char *p;

	if (g_tried) return g_img;
	g_tried = 1;
	if (!(p = getenv("CHAOS_P8_OVERLAY")) || !*p) return NULL;
	if (!(g_img = SDL_LoadBMP(p))) fprintf(stderr, "chaos-overlay: %s: %s\n", p, SDL_GetError());
	else fprintf(stderr, "chaos-overlay: %s, %dx%d\n", p, g_img->w, g_img->h);
	return g_img;
}

void SDL_RenderPresent(SDL_Renderer *r)
{
	static void (*real)(SDL_Renderer *);
	SDL_Surface *img;

	if (!real) real = (void (*)(SDL_Renderer *))dlsym(RTLD_NEXT, "SDL_RenderPresent");
	if (r && (img = overlay())) {
		if (g_tex_for != r) {
			if (g_tex) SDL_DestroyTexture(g_tex);
			g_tex = SDL_CreateTextureFromSurface(r, img);
			g_tex_for = r;
			if (g_tex) SDL_SetTextureBlendMode(g_tex, SDL_BLENDMODE_BLEND);
		}
		if (g_tex) {
			SDL_Texture *target = SDL_GetRenderTarget(r);
			SDL_Rect vp, clip;
			SDL_bool clipped = SDL_RenderIsClipEnabled(r);
			int lw, lh;
			float sx, sy;

			SDL_RenderGetLogicalSize(r, &lw, &lh);
			SDL_RenderGetViewport(r, &vp);
			SDL_RenderGetClipRect(r, &clip);
			SDL_RenderGetScale(r, &sx, &sy);
			if (target) SDL_SetRenderTarget(r, NULL);
			if (lw || lh) SDL_RenderSetLogicalSize(r, 0, 0);
			SDL_RenderSetScale(r, 1, 1);
			SDL_RenderSetViewport(r, NULL);
			SDL_RenderSetClipRect(r, NULL);
			SDL_RenderCopy(r, g_tex, NULL, NULL);
			if (lw || lh) SDL_RenderSetLogicalSize(r, lw, lh);
			SDL_RenderSetScale(r, sx, sy);
			SDL_RenderSetViewport(r, &vp);
			SDL_RenderSetClipRect(r, clipped ? &clip : NULL);
			if (target) SDL_SetRenderTarget(r, target);
		}
	}
	if (real) real(r);
}

int SDL_UpdateWindowSurface(SDL_Window *w)
{
	static int (*real)(SDL_Window *);
	SDL_Surface *img, *s;

	if (!real) real = (int (*)(SDL_Window *))dlsym(RTLD_NEXT, "SDL_UpdateWindowSurface");
	if (w && (img = overlay()) && (s = SDL_GetWindowSurface(w))) {
		SDL_Rect all = { 0, 0, s->w, s->h };

		SDL_SetSurfaceBlendMode(img, SDL_BLENDMODE_BLEND);
		SDL_BlitScaled(img, NULL, s, &all);
	}
	return real ? real(w) : -1;
}
