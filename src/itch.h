/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_ITCH_H
#define CHAOS_ITCH_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* ChaOS: box art from itch.io, where most new homebrew for these consoles is
 * published, for the games libretro's thumbnails (built on No-Intro's lists
 * of released games) don't have.
 *
 * itch.io has no search API. What it does publish is an RSS feed for every
 * tag (https://itch.io/games/tag-nes-rom.xml?page=N, 36 games a page), each
 * game with its title, its page and the cover its developer chose; the feeds
 * ignore a search term (tried 2026-11-12: q= gives the same list). So, as
 * NextUI-Itchio-Pak does (github.com/carroarmato0/NextUI-Itchio-Pak, MIT),
 * the console's whole list is read once onto the card and searched there,
 * then kept fresh from the newest-first feed, which is read only until it
 * reaches a game already known. On 2026-11-12 the lists ran from 10 games
 * (Game Gear) to about 2,000 (Game Boy): a minute or two the first time, a
 * page or two after.
 *
 * Politely: one request at a time, a pause between pages, the user agent
 * naming ChaOS (net.c), and a 429 waited out rather than pressed.
 *
 * The parsing and the matching take no network and no card, so
 * tools/itch-check.c checks them against a feed page itch.io served. */

#define ITCH_PER_PAGE 36

typedef struct {
	char title[128];   /* the developer's title, entities decoded */
	char link[192];    /* https://<author>.itch.io/<game> */
	char image[256];   /* its cover, https://img.itch.zone/... */
} itch_game;

/* The tag feeds a console's games are under, by ChaOS's folder name, ending
 * with NULL; NULL for a console itch.io has no such tag for. More specific
 * first (as the pak orders them). */
const char *const *itch_slugs(const char *folder);

/* The games in one feed page, up to `max`: how many, or -1 when it isn't an
 * itch.io feed at all. An item with no title, or whose link or cover isn't
 * itch.io's own, is left out. */
int itch_feed_parse(const char *xml, size_t len, itch_game *out, int max);

/* A title as it is compared: art_norm_loose's words, less what itch.io titles
 * add for the shop ("(NES)", "for Game Boy", "- A Real NES Game!", "ROM",
 * "Homebrew"), so "Super Tilt Bro. for NES" is "super tilt bro". */
void itch_title_key(const char *title, char *out, size_t n);

/* How alike a ROM's name and an itch.io title are, 0 to 100: 100 the same
 * words, less for words one has and the other hasn't. */
int itch_score(const char *rom_key, const char *title_key);

/* The best matches for a ROM's name among `n` games, best first, into `best`
 * (indexes), at most `max`, and only those that score at least
 * ITCH_SCORE_MIN. Returns how many. */
#define ITCH_SCORE_MIN 50
int itch_match(const char *rom_name, const itch_game *games, int n, int *best, int max);

/* The list kept on the card: a line saying when it was read and whether it
 * is whole, then a game a line, tab-separated. */
bool itch_index_save(const char *path, const itch_game *games, int n, time_t when,
                     bool whole);
/* The list, from malloc for the caller to free, or NULL (count 0) when there
 * is none or it won't read. `when` and `whole` as saved. */
itch_game *itch_index_load(const char *path, int *count, time_t *when, bool *whole);

/* ---- reading a console's list, a step a frame (needs the network) ------- */

/* Starts reading `folder`'s feeds into `index_path`: the whole list when
 * there is none yet, or isn't whole, else only what is new since. False when
 * itch.io has no tag for that console. */
bool itch_fetch_begin(const char *folder, const char *index_path);

/* One step: 1 going, 0 done (the list is on the card), -1 stopped (an error;
 * itch_fetch_problem says which). Starts a request or polls the one going;
 * never waits on the network. */
int  itch_fetch_step(void);
void itch_fetch_cancel(void);

/* Progress, for the panel: games found so far and pages read. */
void itch_fetch_progress(int *games, int *pages);
const char *itch_fetch_problem(void);

#endif
