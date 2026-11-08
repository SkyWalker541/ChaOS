/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_PIXNAME_H
#define CHAOS_PIXNAME_H

#include <SDL.h>
#include <stdbool.h>

/* ChaOS Pixel Names: a system's card as its name in the logo's pixel letters
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

/* For screenshots: cards at their ends at once, or (t >= 0) all at t. */
void pn_fix(float t);

/* Whether any card is still moving: the shelf keeps drawing while one is. */
bool pn_animating(void);

#endif
