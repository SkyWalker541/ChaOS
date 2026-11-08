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
 * Diatom's own. Turbo pulses at the speed the shipped turbo uses (~3: three
 * frames down, three up). No SDL: tools/menu-check.c links it. */

#include <stdbool.h>

#define RM_BUTTONS  10      /* A B X Y L1 R1 L2 R2 Select Start */
#define RM_CHOICES  24
#define RM_TURBO    3

typedef struct {
	const char *target;     /* Diatom's name for the retropad button, or "none" */
	const char *label;      /* what the console calls it */
	bool        turbo;
} rm_choice;

typedef struct { int choice[RM_BUTTONS]; } rm_map;

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

#endif
