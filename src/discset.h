/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_DISCSET_H
#define CHAOS_DISCSET_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: games on more than one disc, made playlists.
 *
 * A multi-disc game is one game on the shelf when it is a folder with an
 * .m3u listing its discs (library.c), and then MENU > Disc swaps them. Not
 * everyone knows to make one, so ChaOS finds the discs that should be one:
 *
 *   loose      "Final Fantasy VII (USA) (Disc 1).chd", "(Disc 2)", "(Disc 3)"
 *              side by side in Roms/PlayStation. They go into a folder named
 *              for the game ("Final Fantasy VII (USA)"), each .cue with the
 *              .bin files it names, and the folder gets
 *              "Final Fantasy VII (USA).m3u".
 *   a folder   a game's folder already holding its discs, but no .m3u: the
 *              .m3u is written there, named for the folder, and nothing moves.
 *
 * A disc is told by its name: "Disc 2", "Disk 2", "CD2", "Disc 2 of 3",
 * "Disc B", in brackets or not. Two or more discs of the same game (the
 * same name with the disc taken out) and no two the same number make a set;
 * the .m3u lists them in disc order. One disc alone, or two files that differ
 * otherwise (a USA and a Europe disc 1) are left as they are.
 *
 * Moving a game takes its things too (ds_apply): the save (Saves/<name>.srm)
 * copied to the name the playlist plays under, the first disc's states
 * (Saves/States/<console>/<file>.*) and its box art. The
 * launcher moves what's in its databases: favorites, play time and the game's
 * scraped info (main.c). No SDL here; tools/discset-check.c checks it. */

#define DS_DISCS 8
#define DS_FILES 32
#define DS_MAX   64

typedef struct {
	char folder[64];             /* the console's folder in Roms */
	char base[200];              /* the game: the playlist's (and folder's) name */
	char dir[256];               /* the game's folder: made, or there already */
	bool loose;                  /* its discs are loose in the console's folder */
	char discs[DS_DISCS][256];   /* the files the playlist lists, in disc order */
	int  n;
	char files[DS_FILES][256];   /* every file that moves (loose): discs and their tracks */
	int  nf;
	char was[512];               /* what the shelf launched before, as in Roms/<folder> */
	char now[512];               /* what it launches after: "<dir>/<base>.m3u" */
	bool clash;                  /* something is in the way: left alone */
} ds_set;

/* The disc a file's name says, 1 up ("Disc B" is 2), and the name with the
 * disc taken out into `base` (trimmed); 0 when it names no disc. */
int  ds_disc_of(const char *name, char *base, size_t bn);

/* The sets in Roms/<folder> (`roms` is the Roms folder), up to `max`; -1
 * when the folder can't be read. */
int  ds_scan(const char *roms, const char *folder, ds_set *out, int max);

/* One set made a playlist, its files and things with it. `shared` is
 * .userdata/shared, `card` the card's root. False if nothing was done. */
bool ds_apply(const char *roms, const char *shared, const char *card, const ds_set *s);

#endif
