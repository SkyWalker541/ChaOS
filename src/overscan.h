/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_OVERSCAN_H
#define CHAOS_OVERSCAN_H

#include <stdbool.h>

/* ChaOS: Crop Overscan - the edge of the picture a television hid behind its
 * bezel, where some games leave flickering tiles or a blank column.
 *
 * Per console, and only for the consoles whose emulator can crop: NES
 * (FCEUmm), TurboGrafx-16 (Beetle PCE Fast) and Master System (Genesis Plus
 * GX). Each choice is a set of the core's own options, kept as the console's
 * core options ("coreopt.<tag>.<key>") and sent with every game. Every choice
 * names every option it touches, the default included: the emulator stays
 * loaded between games, so an option left unsaid would keep the last one's
 * value. The first choice's values are each core's own defaults, so a
 * console nobody has set looks as it always did.
 *
 * Values from each core's libretro_core_options.h at the version ChaOS runs. */

#define OS_KEYS 4

typedef struct {
	const char *label;
	const char *key[OS_KEYS];    /* NULL-terminated if fewer */
	const char *value[OS_KEYS];
} os_choice;

typedef struct {
	const char *tag;             /* systems.cfg's */
	int         def;             /* the choice that is the core's default */
	int         n;
	os_choice   c[3];
} os_console;

/* The console's crops, or NULL: Genesis already hides its borders, and the
 * handhelds and SNES show what they should */
const os_console *os_for(const char *tag);

/* Which choice the stored options are: `get` answers an option's stored
 * value, or NULL for none. The default when they match nothing. */
int os_current(const os_console *c, const char *(*get)(const char *key, void *ctx), void *ctx);

#endif
