/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_PIXNAME_H
#define CHAOS_PIXNAME_H

#include <SDL.h>
#include <stdbool.h>

/* ChaOS Pixel Cards: a system's card as its name in the logo's pixel letters
 * (pixfont.h), drawn here rather than loaded. The card under the cursor has
 * its name's pixels fly in and settle; the others' scatter back out to a faint
 * drift of loose pixels, so scrolling the shelf takes one name apart as it
 * builds the next. About a second each way.
 *
 * A card is a target texture, drawn again only while it moves. The caller
 * owns it like any other card texture (the app's sys_tex), and passes back
 * the one it holds; one it destroyed it passes as NULL. */

#define PN_W 640
#define PN_H 820

/* The card for `name`, the texture `held` drawn again if it needs to be, or a
 * new one. `focused` is whether the cursor is on it. NULL when the renderer
 * cannot make a target texture. */
SDL_Texture *pn_card(SDL_Renderer *r, SDL_Texture *held, const char *name,
                     unsigned accent, bool focused);

/* The ChaOS pixel themes in Cubic, where every face is a game: the system's name
 * over the face after a turn to it, t 0..1 assembling and back to scatter. */
void pn_overlay(SDL_Renderer *r, const char *name, unsigned accent, float t,
                int screen_w, int screen_h);

/* ChaOS Pixel Names: the system's name alone, large, centred on cy, flying
 * together while `focused`, gone at once after; call it for every system each
 * frame (it draws only those still showing). True when it drew. */
bool pn_big(SDL_Renderer *r, const char *name, unsigned accent, bool focused,
            int w, int h, int cy);

/* How long a name takes to form, or to scatter: Transition Speed. */
void pn_speed(int ms);

/* For screenshots: cards at their ends at once, or (t >= 0) all at t. */
void pn_fix(float t);
float pn_fixed(void);   /* what pn_fix set, -2 for animating */

/* Whether any card is still moving: the shelf keeps drawing while one is. */
bool pn_animating(void);

#endif
