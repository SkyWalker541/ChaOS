/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_CHEATS_H
#define CHAOS_CHEATS_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: cheats.
 *
 * A game's cheats are a RetroArch .cht file on the card,
 * Cheats/<console folder>/<game>.cht: one the player put there, or one
 * fetched from libretro's cheat database (libretro-database, cht/, CC BY-SA
 * 4.0). Which are on is kept IN that file, as RetroArch keeps it
 * (cheatN_enable), so the file means the same thing in RetroArch.
 *
 * The cheats that are on go to Diatom as a small file of its own
 * (src/cheats.h in ChaOS-diatom): "C<tab>code" for a code the core decodes,
 * "M<tab>..." for a memory write Diatom makes after each frame.
 *
 * Fetching: the website publishes an index per collection,
 * cheats/<collection>.txt - its first line "# <commit>", then one file name
 * a line, without .cht - and the file itself comes from libretro's repository
 * at that commit. No SDL here: `make check-cheats` runs all of it. */

typedef struct {
	char    *desc;
	char    *code;
	bool     on;
	int      handler;        /* 0: a code for the core; 1: a memory write */
	unsigned address, size, type, value, bitmask, repeat, add_address, add_value;
	bool     big_endian;
} cht_entry;

typedef struct {
	cht_entry *e;
	int        n;
} cht_list;

/* Read a .cht. False if it can't be read or holds no cheats. */
bool cht_read(const char *path, cht_list *out);
void cht_free(cht_list *l);

/* Whether a cheat can be used: a memory write of a kind Diatom makes, or a
 * code, when the console's emulator takes codes (`codes_ok`) and the code
 * asks for no value of the player's ("??"). */
bool cht_usable(const cht_entry *e, bool codes_ok);

/* Which are on, back into the file: its cheatN_enable lines rewritten, and
 * any it lacks added. Everything else in it is left as it was. */
bool cht_save_enables(const char *path, const cht_list *l);

/* The cheats that are on and usable, for Diatom. Returns how many; writes
 * an empty file for none. -1 if it can't be written. */
int  cht_write_active(const char *path, const cht_list *l, bool codes_ok);

/* Whether a console's emulator decodes cheat codes (systems.cfg's core) */
bool cht_core_takes_codes(const char *core);

/* Where a game's cheats are: <card>/Cheats/<folder>/<stem>.cht */
void cht_path(const char *card, const char *folder, const char *rom_file, char *out, size_t n);

/* ---- the index ----------------------------------------------------------- */

typedef struct {
	char   commit[48];
	char **names;      /* without .cht, as listed */
	int    n;
} cht_index;

/* From the text of an index file. False when it isn't one. */
bool cht_index_parse(const char *text, cht_index *out);
void cht_index_free(cht_index *x);

/* The files that are this game: the same title once region, language and
 * revision tags are set aside (artscrape's art_norm), best first (its
 * art_tag_score). Fills `idx` with up to `max` positions in x->names;
 * returns how many. */
int  cht_candidates(const cht_index *x, const char *rom_file, int *idx, int max);

/* Where, in the whole list, a title would sit: for opening the full list
 * near the game when nothing matched. */
int  cht_nearest(const cht_index *x, const char *rom_file);

/* The URLs: the index on the website, and a file at the index's commit */
void cht_index_url(const char *collection, char *out, size_t n);
void cht_file_url(const cht_index *x, const char *collection, const char *name,
                  char *out, size_t n);

#define CHT_SITE "https://skywalker541.github.io/ChaOS/cheats/"
#define CHT_RAW  "https://raw.githubusercontent.com/libretro/libretro-database/"

#endif
