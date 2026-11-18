/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_HBCAT_H
#define CHAOS_HBCAT_H

#include <stdbool.h>
#include <stddef.h>

#include "itch.h"

/* ChaOS: Get Homebrew's catalogue, the parts with no SDL and no network, so
 * tools/hbcat-check.c can check them.
 *
 * Every console's itch.io list (itch.h) in one catalogue, as
 * NextUI-Itchio-Pak shows them (github.com/carroarmato0/NextUI-Itchio-Pak,
 * MIT): narrowed to a console and to what it costs, searched by title and
 * author, and sorted. A game's own page adds what the feed doesn't have, from
 * its data.json (price, suggested price, sale, screenshots, tags) and its
 * page (the description); both are read here. Content warnings are by tag, as
 * the pak's are (its internal/itchio/advisory.go): a game tagged with one the
 * player has asked to be warned about says so before its page shows. */

/* What the catalogue knows of a game besides the feed's */
enum {
	HB_HAVE   = 1,    /* downloaded to this Brick, or a file of its name is here */
	HB_UPDATE = 2,    /* downloaded, and itch.io has a newer file */
	HB_GONE   = 4,    /* downloaded, and gone from itch.io */
	HB_OWNED  = 8,    /* bought, by the account of the key kept */
};

typedef struct {
	itch_game g;
	int  sys;              /* the console's place in the launcher's list */
	int  order;            /* its place in the console's list */
	int  rank;             /* the console's place in the pak's order (Popular's ties) */
	char author[48];       /* from the page's address: <author>.itch.io */
	unsigned char flags;
} hb_game;

/* Popular is itch.io's own order, the feeds' (the pak's "RSS"); Newest by
 * the date each was published */
typedef enum { HB_SORT_POP, HB_SORT_NEW, HB_SORT_AZ, HB_SORT_ZA, HB_SORT_DL, HB_SORTS } hb_sort;
typedef enum { HB_SHOW_ALL, HB_SHOW_FREE, HB_SHOW_PAID, HB_SHOW_OWNED, HB_SHOW_HAVE, HB_SHOWS } hb_show;

typedef struct {
	hb_sort sort;
	hb_show show;
	int  sys;              /* -1: every console */
	char search[64];       /* "": everything; else every word in the title or author */
} hb_view;

const char *hb_sort_name(hb_sort s);   /* "Popular", "Newest", "A-Z", "Z-A", "Downloaded First" */
const char *hb_show_name(hb_show s);   /* "All Games", "Free", "Paid", "Owned", "On This Brick" */

/* The author from a game's address, "" when it isn't <author>.itch.io */
void hb_author_of(const char *link, char *out, size_t n);

/* Whether a game is one the search asks for: every word of `search`, any
 * case, in its title or its author */
bool hb_matches(const hb_game *g, const char *search);

/* The games the view shows, as indexes into `games`, sorted: how many */
int  hb_view_apply(const hb_game *games, int n, const hb_view *v, int *out);

/* The letter a title files under for L2/R2: A-Z, or '#' */
char hb_initial(const char *title);

/* ---- a game's own page ---- */

#define HB_SHOTS 8
#define HB_TAGS  32

typedef struct {
	char price[16];        /* "" free, "$0.00" name your own price, else the least */
	char suggested[16];    /* the developer's suggested amount, "" none */
	char original[16];     /* on sale: what it was */
	int  sale;             /* on sale: percent off, 0 not */
	char cover[256];
	char shots[HB_SHOTS][256];
	int  nshots;
	char tags[HB_TAGS][40];
	int  ntags;
	char authors[96];      /* "Name, Name" */
} hb_detail;

/* A game's data.json. False when it isn't one. */
bool hb_data_parse(const char *json, size_t len, hb_detail *d);

/* What it costs, a line: "Free", "Name your own price", "Name your own
 * price, $2.00 suggested", "$4.99", "$2.99 (was $4.99, 40% off)" */
void hb_price_line(const hb_detail *d, char *out, size_t n);

/* The description on a game's page (its formatted_description), as plain
 * text: paragraphs and headings each a line with a blank line between, list
 * items "- ", entities decoded, links' text kept, images and embeds dropped.
 * How long; 0 when the page has none. */
size_t hb_desc_parse(const char *html, char *out, size_t n);

/* ---- content warnings ---- */

typedef enum { HB_CW_ADULT, HB_CW_HEAVY, HB_CW_SUBSTANCE, HB_CWS } hb_cw;

const char *hb_cw_name(hb_cw c);        /* "Adult Content", ... */
const char *hb_cw_hint(hb_cw c);
/* The category's tags, NULL-ended */
const char *const *hb_cw_tags(hb_cw c);
/* Whether it warns by default */
bool hb_cw_default(hb_cw c);

/* The tags of `d` that warn, given which categories are on (`on`, a bit per
 * hb_cw) and which tags were turned off (`off(tag, ctx)` true), into `out` as
 * "tag, tag"; how many */
int  hb_cw_hits(const hb_detail *d, unsigned on, bool (*off)(const char *tag, void *ctx), void *ctx,
                char *out, size_t n);

/* ---- what was downloaded ---- */

/* A line of downloaded.list: the game's page, where it went (under Roms), the
 * file's upload id on itch.io and its size then. The first two are all a
 * line had before 2.6.0; the others are "" and 0 for those. */
typedef struct {
	char link[192];
	char rel[512];
	char upload[24];
	long size;
} hb_have_rec;

bool hb_have_line(const char *line, hb_have_rec *r);

#endif
