/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_LBOX_H
#define CHAOS_LBOX_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: box art from the LaunchBox Games Database (gamesdb.launchbox-app.com),
 * to search and pick by hand. Aquilus's suggestion, 2026-11-19: it has covers
 * for romhacks and homebrew that libretro's No-Intro-named thumbnails never
 * will, and needs no account.
 *
 * It has no API, so this reads the two web pages a person would use, as
 * served (no script runs on them):
 *
 *   /games/results/<words>?searchText=<words>
 *       a card per game: <a href="/games/details/<id>-slug"> ... <h3>title</h3>
 *       <p>platform</p>
 *   /games/images/<id>
 *       a tile per picture: <a href="https://images.launchbox-app.com/<guid>.png"
 *       data-title="<game> - Box - Front Image (North America)"> ...
 *       <img src="<thumbnail>">
 *
 * Read 2026-11-19. If LaunchBox changes its pages these find nothing, and the
 * screen says nothing was found rather than anything worse. The parsers are
 * separate from the fetching so tools/lbox-check can hold them to the pages
 * as they were. */

#define LBOX_STR 160

typedef struct {
	int  id;
	char title[LBOX_STR];
	char platform[64];
} lbox_game;

typedef struct {
	char kind[64];       /* "Box - Front", "Screenshot - Game Title", ... */
	char region[48];     /* "North America", "" when none */
	char url[256];       /* the picture */
	char thumb[256];     /* a smaller one, "" when none */
} lbox_image;

/* The games a search page lists, in its order, at most `max`. */
int lbox_parse_results(const char *html, lbox_game *out, int max);

/* A game's pictures that can be a cover, best first: box fronts (the region
 * `prefer` first, if given), then 3D boxes, cartridges, title screens. */
int lbox_parse_images(const char *html, lbox_image *out, int max, const char *prefer);

/* LaunchBox's name for a console, from ChaOS's tag ("gba"), or NULL. */
const char *lbox_platform(const char *tag);

/* Fetching: blocking, a few seconds at most. -1 when the site can't be
 * reached, else how many were found. The search puts the console's own games
 * first when `tag` names one. */
int lbox_search(const char *words, const char *tag, lbox_game *out, int max);
int lbox_images(int id, const char *tag, lbox_image *out, int max);

#endif
