/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TORTOS_CARDS_H
#define TORTOS_CARDS_H

#include <stdbool.h>
#include <string.h>

/* Which art the shelf wears.
 *
 * A set is a directory under res/cards/ holding files named by column 6 of
 * systems.cfg, so adding one is dropping in a folder - no config change and no
 * code change. The art does NOT have to match the default set's shape: cf_draw
 * contain-fits every texture and lengthens the reflection for short art, which
 * is why a set of square photographs sits on the same shelf as portrait cards
 * without touching the layout.
 *
 * ONE CONVENTION, AND IT IS LOAD-BEARING: art wider than it is tall is CROPPED
 * to its own edges, and art taller than wide keeps whatever canvas it has.
 *
 * The shelf reads a texture's shape to decide how big to draw it
 * (cf_layout.wide_area, CF_WIDE_ART), and padding lies about shape. Every
 * fancy card used to be a 384x384 canvas with the console centered in
 * transparency, so a Master System - 369x180 of actual console - was drawn as
 * a SQUARE: 466x227 inside a 485x622 frame, two thirds the area of a Game Boy
 * Color, with 395px of frame height empty. Cropping the nine wide ones on
 * 2026-09-16 is what let the shelf see them.
 *
 * The tall ones were deliberately NOT cropped, and that is the part worth
 * knowing before you tidy it. Game Boy Color's ink is 267x369 in that square,
 * and cropping it would let contain-fit grow it from 466px tall to the frame's
 * full 622 - straight through the system name at y=618. They already fill
 * their frame; the padding is what holds them where they belong.
 *
 * Accents stay in systems.cfg and do not vary by set. They were sampled from
 * the classic art, and the obvious idea of resampling them per set does not
 * survive contact with the photographs: measured over their opaque pixels the
 * hardware runs 0-13% saturation, Genesis at 0, because consoles are gray and
 * black plastic. There is no color in them to sample. */
#define CARDS_DEFAULT "classic"

typedef struct {
	/* What the setting stores. Separate from `dir` because a preset is art
	 * AND how it is presented, and two presets can draw the same directory a
	 * different way. Keying the setting on the directory would make those two
	 * indistinguishable once written down. */
	const char *id;
	const char *dir;   /* directory under res/cards/ */
	const char *name;  /* what the menu row calls it */
	/* Whether the art says which system it is. The classic cards have the
	 * name drawn into the image, so the shelf must not print it again; a set
	 * of bare photographs does not, and without this the shelf is unlabeled
	 * and you are asked to tell a Master System from a Genesis by silhouette.
	 * A property of the set, so a new one declares which kind it is rather
	 * than the shelf special-casing a directory by name. */
	bool labeled;
	/* Clear air between the art and its reflection, as a fraction of the
	 * card's half height - so it scales with the card rather than being a
	 * fixed number of screen pixels, which would sit wrong on a side card
	 * that is scaled and yawed.
	 *
	 * Zero for a drawn card, because a card IS a thing resting on a
	 * reflective surface and contact is what that looks like. A photograph of
	 * a console is an object above the surface, and the gap is what says so.
	 * Applies to system art only: box art is card-like whatever the theme. */
	float reflect_gap;
} card_set;

static const card_set CARD_SETS[] = {
	{ "classic", "classic", "Plain Jane",  true,  0.00f },
	{ "fancy",   "fancy",   "Fancy Pants", false, 0.21f },   /* about 50px */
	/* ChaOS: no files - each card is drawn, its name in the logo's pixel
	 * letters, assembling under the cursor (pixname.h). CARDS_DRAWN. */
	{ "pixel",   "",        "ChaOS Pixel Cards", true, 0.00f },
	/* ChaOS: no cards at all - the name alone, large (pixname.h pn_big) */
	{ "pixelbig", "",       "ChaOS Pixel Names", true, 0.00f },
};
/* A set drawn by the launcher rather than loaded: no directory */
#define CARDS_DRAWN(i) (!CARD_SETS[i].dir[0])
/* The one with no cards: names alone, filling the screen */
#define CARDS_BIG(i) (!strcmp(CARD_SETS[i].id, "pixelbig"))
#define CARD_SET_COUNT ((int)(sizeof CARD_SETS / sizeof CARD_SETS[0]))

/* The set a stored name selects, by index. An unknown name is the default
 * rather than an error: the setting outlives the directory it points at, and a
 * set that has been removed should leave a working shelf behind. */
static inline int cards_index(const char *id)
{
	int i;

	if (id && *id)
		for (i = 0; i < CARD_SET_COUNT; i++)
			if (!strcmp(CARD_SETS[i].id, id)) return i;
	return 0;
}

/* Stepping wraps, because two entries with a stop at each end would make the
 * row feel broken in one direction half the time. */
static inline int cards_step(int i, int d)
{
	int n = CARD_SET_COUNT;

	if (n <= 0) return 0;
	i = (i + d) % n;
	return i < 0 ? i + n : i;
}

/* THE ORIGIN IS THE BOTTOM LEFT, and the index grows with x and with y.
 *
 * Screens number rows downward, so the vertical shelf looks inverted to
 * anyone who assumes that: the first item is at the BOTTOM, the last at the
 * top, and up advances. It is not inverted, it is Cartesian, and it is the
 * same rule the horizontal shelf has always followed - index 0 at the left,
 * growing rightward, in cf_draw and in the rail alike.
 *
 * Written down because it is worth more as one rule than as two conventions
 * that happen to agree. Anything that reads as backwards in one direction
 * should be checked against this before being corrected: ui_rail_v inverts
 * its position on purpose, and so does cf_draw_cube's rotation sign.
 *
 * Which way a shelf runs. Its own setting rather than more entries in the
 * table above, because it is orthogonal to the art: all three themes read
 * sensibly either way, so folding it in would mean six presets to name and
 * keep consistent instead of three plus a direction. Applies to the games
 * shelf as well as the systems one. */
#define CARDS_DIR_DEFAULT "horizontal"

typedef struct {
	const char *id;
	const char *name;
	bool vertical;
	/* One surface instead of two: up and down turn to another system, left
	 * and right move through that system's games. There is no entering and
	 * no going back, because what you are looking at is already the thing
	 * you can act on. Implies `vertical` - the system axis is still the
	 * vertical one - so anything asking "which way do systems run" keeps
	 * working without knowing this mode exists. */
	bool both;
} card_dir;

static const card_dir CARD_DIRS[] = {
	{ "horizontal", "Horizontal", false, false },
	{ "vertical",   "Vertical",   true,  false },
	/* Stored as "both" and shown as "Cubic": the id and the field name say
	 * what it does - two axes on one surface - and the label says what it
	 * looks like, which is the thing a person is choosing between. */
	{ "both",       "Cubic",      true,  true  },
};
#define CARD_DIR_COUNT ((int)(sizeof CARD_DIRS / sizeof CARD_DIRS[0]))

static inline int cards_dir_index(const char *id)
{
	int i;

	if (id && *id)
		for (i = 0; i < CARD_DIR_COUNT; i++)
			if (!strcmp(CARD_DIRS[i].id, id)) return i;
	return 0;
}

static inline int cards_dir_step(int i, int d)
{
	int n = CARD_DIR_COUNT;

	if (n <= 0) return 0;
	i = (i + d) % n;
	return i < 0 ? i + n : i;
}

/* Which directions a card set can show. The ChaOS themes are drawn, not
 * loaded, and each leaves some out: Pixel Cards has no Cubic, because its
 * cards are not built for two axes, and Pixel Names has only Horizontal -
 * names alone, large, read left to right. The other sets offer all three. */
static inline bool cards_dir_allowed(int set, int dir)
{
	if (set < 0 || set >= CARD_SET_COUNT || dir < 0 || dir >= CARD_DIR_COUNT)
		return false;
	if (CARDS_DRAWN(set))
		return CARDS_BIG(set) ? !CARD_DIRS[dir].vertical : !CARD_DIRS[dir].both;
	return true;
}

/* The nearest allowed direction to `dir` for this set, moving by `d`, or
 * `dir` itself when none is. Wraps like cards_dir_step. */
static inline int cards_dir_step_for(int set, int dir, int d)
{
	int n = CARD_DIR_COUNT, k, step = d < 0 ? -1 : 1;

	for (k = 0; k < n; k++) {
		dir = (dir + step + n) % n;
		if (cards_dir_allowed(set, dir)) return dir;
	}
	return dir;
}

/* A direction the set can show: `dir` if allowed, else Horizontal, which
 * every set can show. Used when the set changes under a stored direction. */
static inline int cards_dir_fit(int set, int dir)
{
	return cards_dir_allowed(set, dir) ? dir : 0;
}

#endif
