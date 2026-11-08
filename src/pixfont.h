/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_PIXFONT_H
#define CHAOS_PIXFONT_H

/* ChaOS: the logo's pixel letters (tools/genlogo.py draws "ChaOS" in them),
 * for the ChaOS Pixel Cards and Pixel Names themes: every capital, the digits and a few marks,
 * 5x7 (I and 1 and the marks narrower), a column between letters.
 *
 * A name is laid out in capitals, at most PF_LINE_MAX letters to a line,
 * broken at spaces - and inside a long word where its capitals say so
 * ("TurboGrafx-16" is "TURBO" over "GRAFX-16") - with its last word in the
 * console's accent, as OS is orange in the logo.
 *
 * No SDL here: tools/pixfont-check.c links it. */

#define PF_LINE_MAX  9
#define PF_LINES_MAX 4
#define PF_GLYPH_H   7
#define PF_LINE_GAP  2           /* empty rows between lines */
#define PF_PTS_MAX   2048

typedef struct {
	short x, y;                  /* in letter-pixels, from the block's top left */
	unsigned char accent;        /* in the last word */
} pf_pt;

/* The name's pixels, centred line by line in a block cols x rows. Returns how
 * many (at most max); 0 for a name with nothing drawable. */
int pf_layout(const char *name, pf_pt *pts, int max, int *cols, int *rows);

/* The lines the name breaks into, for checking: returns how many, each in
 * capitals into lines[i]. */
int pf_lines(const char *name, char lines[PF_LINES_MAX][32], int *accent_from);

/* Whether a character has a glyph (anything else is drawn as a space). */
int pf_has(char c);

#endif
