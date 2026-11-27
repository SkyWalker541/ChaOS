/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_REMAP_H
#define CHAOS_REMAP_H

/* ChaOS: Button Mapping - which of the console's buttons each of the Brick's
 * presses, or that button with turbo, or nothing.
 *
 * Diatom does the work (SETMAP, src/env.c there): a whole table of
 * `brick:console[~period]` pairs in its canonical names, sent after RUN and
 * again whenever it changes in a game. This file only knows, per console,
 * what its pad's buttons are called - by the core that runs it, from its
 * input descriptors (Genesis Plus GX calls the retropad's Y button "A") - and
 * turns a table of choices into that line and back.
 *
 * Saved per console, and per game over it (src/main.c). The d-pad and MENU
 * are not remapped: the d-pad is the same on every console, and MENU is
 * Diatom's own. Turbo pulses at the map's Turbo Speed, the same for every
 * turbo button in it: Normal is the speed the shipped turbo uses (~3: three
 * frames down, three up, ten presses a second). Turbo Speed was Trebuchet's
 * idea. No SDL: tools/remap-check.c links it. */

#include <stdbool.h>

#define RM_BUTTONS  10      /* A B X Y L1 R1 L2 R2 Select Start */
#define RM_CHOICES  24
#define RM_TURBO    3

typedef struct {
	const char *target;     /* Diatom's name for the retropad button, or "none" */
	const char *label;      /* what the console calls it */
	bool        turbo;
} rm_choice;

/* Turbo Speed: half a press, in the console's frames, as Diatom takes it
 * (`x:a~3`); the presets are slowest first, and a map's period is one of
 * them unless the line it came from said otherwise */
#define RM_SPEEDS   4
#define RM_PERIOD_MAX 30    /* Diatom's own limit (TURBO_PERIOD_MAX) */

typedef struct {
	int choice[RM_BUTTONS];
	int period;             /* every turbo button's, 1 to RM_PERIOD_MAX */
} rm_map;

/* Button Mapping's rows, by what each is (main.c's rm_build and rm_key act on
 * these ids, never on positions): 0 to RM_BUTTONS - 1 are the buttons */
enum { RM_ROW_SPEED = 99, RM_ROW_SCOPE = 100, RM_ROW_RESET, RM_ROW_NONE };
#define RM_ROWS_MAX 24
/* The page's rows' ids in order, with the "For" row (in a game) or without;
 * RM_ROW_NONE for each rule. Returns how many. */
int rm_rows(bool scope, int *ids, int max);

/* The Brick's button i: its name for Diatom ("l1") and on screen ("L1") */
const char *rm_button_id(int i);
const char *rm_button_label(int i);

/* What a button can be set to on this console: its buttons, those with
 * turbo, and None, in that order. Returns how many. */
int rm_choices(const char *tag, rm_choice *out);

/* A SETMAP line (or NULL / "" for the plain one) into choices; anything this
 * console has no button for reads as None */
void rm_from_spec(const char *tag, const char *spec, rm_map *m);
/* And back: every button, whole. False if it does not fit. */
bool rm_to_spec(const char *tag, const rm_map *m, char *out, int outn);

/* "Turbo C", "Start", "None" */
const char *rm_choice_label(const char *tag, int choice, char *buf, int n);

/* Whether any button in the map is a turbo one: the speed is kept in the
 * turbo buttons' own `~period`, so with none it has nowhere to be saved and
 * its row rests (main.c) */
bool rm_has_turbo(const char *tag, const rm_map *m);

/* Turbo Speed: "Normal, 10/s", or "Custom" for a period no preset has */
const char *rm_speed_label(int period, char *buf, int n);
/* The next preset, `d` (-1 or 1) along, round the four; a custom period
 * goes to the preset nearest it in that direction */
int rm_speed_step(int period, int d);

#endif
