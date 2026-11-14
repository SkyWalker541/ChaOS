/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_PICO8_H
#define CHAOS_PICO8_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: PICO-8, Lexaloffle's fantasy console, played with the player's own
 * copy. ChaOS carries none of it: whoever bought PICO-8 copies two files from
 * its Raspberry Pi download - pico8_64 (the program, for 64-bit ARM) and
 * pico8.dat (its data) - into the card's PICO-8 folder, or drops the whole
 * download's zip there and ChaOS takes the two out of it.
 *
 * With both there, the PICO-8 shelf shows: Splore first, PICO-8's own
 * browser of the carts people share, then the carts in Roms/PICO-8 (.p8 and
 * .p8.png). It runs as a port does, with the SDL ports have
 * (res/pico8/run.sh), and its settings and cart saves are kept in the
 * userdata, so they stay through updates.
 *
 * Splore downloads with wget, which on the Brick is BusyBox's and can't do
 * the https every cart comes over; res/pico8/bin/wget is a wget that hands
 * PICO-8's two kinds of call to curl, as spruceUI's does. */

/* systems.cfg's core field for the PICO-8 shelf */
#define PICO8_CORE "pico8"

/* The card's folder for the player's files, beside Roms */
#define PICO8_DIR "PICO-8"

/* The shelf's first card */
#define PICO8_SPLORE_FILE ".splore"
#define PICO8_SPLORE_NAME "Splore"

typedef enum {
	P8_READY,      /* pico8_64 and pico8.dat, and pico8_64 is for this CPU */
	P8_NONE,       /* neither: nobody has put PICO-8 here */
	P8_NO_DAT,     /* the program without its data */
	P8_NO_BIN,     /* the data without the program */
	P8_WRONG_CPU,  /* a pico8_64 that isn't 64-bit ARM (another download's) */
} p8_status;

/* What the folder `dir` (the card's PICO-8 folder) holds */
p8_status p8_check(const char *dir);

/* For the shelf and its messages: what to do about a status, a line */
const char *p8_status_say(p8_status s);

/* What an ELF file is built for: 64 for 64-bit ARM, 32 for 32-bit ARM,
 * -1 for something else (or not ELF at all) */
int p8_elf_arm(const char *path);

/* A zip in `dir` that looks like PICO-8's Raspberry Pi download
 * (pico-8_<version>_raspi.zip, whatever the case), the newest by name;
 * false for none */
bool p8_find_zip(const char *dir, char *out, size_t n);

/* After a zip was unpacked into `stage`: the two files found anywhere in it
 * and moved into `dir`, replacing older ones. False if either is missing. */
bool p8_take_from(const char *stage, const char *dir);

/* A cart's file name, as the shelf lists it: "celeste.p8.png" and
 * "celeste.p8" are carts, "notes.png" is not */
bool p8_is_cart(const char *file);

#endif
