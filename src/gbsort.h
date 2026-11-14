/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_GBSORT_H
#define CHAOS_GBSORT_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: Game Boy games into the folder their cartridge is for.
 *
 * Game Boy and Game Boy Color take each other's files (systems.cfg) and play
 * each on its own machine wherever it is (gamecore.h), so a game in the
 * "wrong" folder works. This tidies it: a cartridge made for the Color (its
 * header's 0x143 top bit, 0x80 or 0xC0) belongs in Game Boy Color, any other
 * in Game Boy. Only files a Game Boy shelf lists (.gb .gbc .cgb .dmg .zip),
 * and only ones whose header can be read.
 *
 * What's found is the player's to move or not: ChaOS asks once, in the
 * background after the update that brings this, after ChaOS Transfer sends
 * anything and once a week, and again whenever MENU >
 * ROM Management > Sort Game Boy Games is chosen.
 *
 * A move takes the game's things with it. The files are here (gbs_move_files):
 * the ROM, its box art (Roms/<folder>/.media/<name>.*), its states and their
 * pictures (.userdata/shared/.tortos/<folder>/<file>.*) and its cheats
 * (Cheats/<folder>/<name>.cht). The launcher moves what's in its databases:
 * favorites, play time (and with it Recently Played), scraped game info and
 * RomM's record of it. Saves need nothing: they are Saves/<name>.srm, one
 * folder for every console.
 *
 * The answers are kept (gbsort.cache in the userdata, by file, size and
 * date), so a second look reads only what's new. */

#define GBS_GB  "Game Boy"
#define GBS_GBC "Game Boy Color"
#define GBS_MAX 1024

typedef struct {
	char file[256];      /* relative to Roms/<from> */
	char name[256];      /* its name on the shelf: the file less its extension */
	const char *from, *to;   /* GBS_GB or GBS_GBC */
	bool clash;          /* <to> already has a file of that name: left alone */
} gbs_move;

/* The games in the other folder for their cartridge, up to `max`. `cache` may
 * be NULL. -1 if neither folder could be read. */
int gbs_scan(const char *roms, const char *cache, gbs_move *out, int max);

/* The files of one move. `shared` is .userdata/shared, `card` the card's
 * root. False if the ROM itself couldn't be moved (then nothing was). */
bool gbs_move_files(const char *roms, const char *shared, const char *card, const gbs_move *m);

/* Which folder a header's flag belongs in */
const char *gbs_home(int flag);

#endif
