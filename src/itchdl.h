/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_ITCHDL_H
#define CHAOS_ITCHDL_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: games downloaded from itch.io, for ROM Management > Get Homebrew.
 *
 * Two ways in, as NextUI-Itchio-Pak has them (github.com/carroarmato0/
 * NextUI-Itchio-Pak, MIT, its internal/itchio/download.go and
 * download_auth.go):
 *
 *   the web    A free game, no account: what itch.io's own Download button
 *              does. The game's page gives a CSRF token; POSTing it to
 *              <game>/download_url gives a signed download page; that page
 *              lists the files (each a data-upload_id) with a token of its
 *              own; POSTing that and the download key's id to
 *              <game>/file/<upload> gives the file's address on itch.io's
 *              CDN. One cookie jar throughout, as a browser keeps one. It is
 *              itch.io's page and not an API, so it can change under us; the
 *              pak's author calls it brittle, and so is this.
 *
 *   the API    With the player's own itch.io API key (itch.io, Settings, API
 *              keys): free games and the ones they bought. The game's id from
 *              <game>/data.json, the purchase (a download key) from
 *              api.itch.io/profile/owned-keys, the files from
 *              api.itch.io/games/<id>/uploads, and the file's address from
 *              api.itch.io/uploads/<id>/download, whose redirect is read and
 *              not followed, so the key goes to api.itch.io alone.
 *
 * The key is the player's, kept in .userdata (0600), never shipped and never
 * logged. No SDL here, and every call blocks (the launcher runs them behind a
 * panel). The page parsing takes no network: tools/itchdl-check.c checks it. */

#define ITCHDL_UPLOADS 24

typedef struct {
	char id[24];       /* itch.io's upload id */
	char name[192];    /* its file name, "game.gbc" */
	long size;         /* bytes, 0 when not known */
} itchdl_upload;

/* A download's state between its steps: the web flow's tokens and jar, or
 * the API's game and purchase */
typedef struct {
	char link[192];          /* the game's page */
	char jar[160];           /* the web flow's cookie jar, in /tmp */
	char csrf[160];          /* the download page's token */
	char key_id[32];         /* the download key's id: the web's, or the purchase's */
	char game_id[24];        /* the API's */
	const char *api_key;     /* NULL: the web */
	int http;                /* the answer to the game's own page (or data.json): 404 or 410, gone */
} itchdl;

/* The key kept, or "" (false) */
bool itchdl_key_load(const char *dir, char *key, size_t n);
/* Kept 0600; an empty key removes it */
bool itchdl_key_save(const char *dir, const char *key);
/* Whose key it is: the account's name into `user`. 1 good, 0 refused, -1
 * itch.io didn't answer. */
int  itchdl_key_check(const char *key, char *user, size_t n);

/* The files a game offers, up to ITCHDL_UPLOADS, starting `d` (whose link
 * and api_key are set). How many, or -1 with `why`. 0 is a game with no
 * download at all (one played in a browser). A paid game through the web,
 * or one the key's account doesn't own, is -1 with `why` saying so and
 * *paid set. */
int  itchdl_uploads(itchdl *d, itchdl_upload *out, int max, bool *paid, char *why, size_t wn);

/* The address one file downloads from (a CDN's, short-lived, carrying its
 * own token: not logged). False with `why`. */
bool itchdl_file_url(itchdl *d, const itchdl_upload *u, char *url, size_t n, char *why, size_t wn);

/* The games the key's account owns: their pages into `links`, up to `max`.
 * How many, or -1 when itch.io didn't answer or refused the key. */
int  itchdl_owned(const char *key, char (*links)[192], int max);

/* The web flow's jar, removed */
void itchdl_done(itchdl *d);

/* ---- the parts that read a page, for the check ---- */

/* A page's csrf_token (value= or content=), "" when there is none */
bool itchdl_csrf(const char *html, char *out, size_t n);
/* The files on a signed download page: each <div class="upload"> with a
 * data-upload_id and a <strong class="name" title=...> */
int  itchdl_parse_page(const char *html, itchdl_upload *out, int max);
/* The download key's numeric id, from the key in a signed URL's last part
 * (a JWT-like "payload.signature", payload base64 JSON with "id") */
bool itchdl_key_id(const char *signed_url, char *out, size_t n);

/* A game's files in the order to try them for a console, best first, into
 * `order`; how many are worth trying (0: none for this console). A file
 * whose extension is in `exts` (a systems.cfg list, "gb,dmg,gbc,cgb,zip")
 * comes first, earlier in the list first; then a zip; then a file with no
 * extension, which itch.io's download page often gives (it shows the
 * developer's label, "Criss Cross Cove (GB Rom)", not the file's name) and
 * is told apart by its first bytes once it is here (itchdl_sniff). As the
 * pak chooses, a .gbc comes before a .gb for either Game Boy, and a .p8.png
 * before a .p8. A label
 * for another machine or for a browser ("web", "Analogue Pocket", "source",
 * "Windows", "soundtrack", ...) goes after the rest, and files that are
 * plainly something else (a .pdf, a .png, an .exe, another console's ROM)
 * are left out. *several: more than one file is as good as the best, so the
 * player is asked. */
int  itchdl_rank(const itchdl_upload *u, int n, const char *exts, int *order, bool *several);
/* Whether a file name has one of `exts` (zip aside when `zip` is false). A
 * PICO-8 cart can be a picture: "png" counts only as ".p8.png". */
bool itchdl_ext_in(const char *name, const char *exts, bool zip);

/* What a file is, from its first bytes: its console's folder (systems.cfg's
 * name, "Game Boy Color") and the extension it should have ("gbc"), or
 * false. A zip is folder "" and extension "zip". Game Boy by its logo and
 * the Color flag at 0x143, Game Boy Advance by its logo at 4, NES by
 * "NES\x1A", Genesis by "SEGA" at 0x100, Master System and Game Gear by
 * "TMR SEGA" and the region after it. PICO-8 ("PICO-8", "p8" or "p8.png")
 * by a text cart's first line, or a PNG 128 pixels wide. */
bool itchdl_sniff(const char *path, char *folder, size_t fn, char *ext, size_t en);

#endif
