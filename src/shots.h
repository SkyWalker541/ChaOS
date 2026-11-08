/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_SHOTS_H
#define CHAOS_SHOTS_H

/* ChaOS: screenshots, per game, for looking back at - a password screen, a
 * code, a map. Taken from the in-game menu (the paused frame, as the game drew
 * it, before any shader), and looked at from there, full screen, one game's
 * at a time: only the game being played.
 *
 * Kept on the card at Screenshots/<console folder>/<game>/, one PNG each,
 * named by when it was taken, "2026-11-08 06.45.12.png" (FAT has no colons),
 * so they sort oldest to newest by name and are easy to find from a computer
 * or ChaOS Transfer. Outside ChaOS's own files, so updates leave them.
 *
 * No SDL here: tools/shots-check.c links it. */

#include <stddef.h>
#include <time.h>

#define SHOTS_MAX  500          /* one game's, listed; more are left unlisted */
#define SHOTS_NAME 40

/* The game's folder: card/Screenshots/<folder>/<rom's name less its
 * extension>. False when it does not fit. */
int shots_dir(const char *card, const char *folder, const char *rom,
              char *out, size_t n);

/* The game's screenshots' file names, newest first. Returns how many (at most
 * max); 0 for none or no folder. */
int shots_list(const char *dir, char (*names)[SHOTS_NAME], int max);

/* The path a new one taken at t goes to, the folders made: a name not yet
 * used. False when the folders cannot be made. */
int shots_new_path(const char *dir, time_t t, char *out, size_t n);

/* Delete one, and the game's folder and its console's with it once empty.
 * False when the file could not be removed. */
int shots_delete(const char *dir, const char *name);

/* When it was taken, from its name, for the viewer: "Nov 8, 6:45 AM".
 * The name itself when it is not one of ours. */
void shots_label(const char *name, char *out, size_t n);

#endif
