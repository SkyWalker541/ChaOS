/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: the logo's pixel letters. See pixfont.h. Original artwork, drawn
 * here pixel by pixel; C, O, S and the shapes of the rest follow the logo's. */
#include "pixfont.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct { char c; const char *row[PF_GLYPH_H]; } glyph;

static const glyph GLYPHS[] = {
	{ 'A', { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
	{ 'B', { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." } },
	{ 'C', { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." } },
	{ 'D', { "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####." } },
	{ 'E', { "#####", "#....", "#....", "####.", "#....", "#....", "#####" } },
	{ 'F', { "#####", "#....", "#....", "####.", "#....", "#....", "#...." } },
	{ 'G', { ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####" } },
	{ 'H', { "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" } },
	{ 'I', { "###", ".#.", ".#.", ".#.", ".#.", ".#.", "###" } },
	{ 'J', { "..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.." } },
	{ 'K', { "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#" } },
	{ 'L', { "#....", "#....", "#....", "#....", "#....", "#....", "#####" } },
	{ 'M', { "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#" } },
	{ 'N', { "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#" } },
	{ 'O', { ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
	{ 'P', { "####.", "#...#", "#...#", "####.", "#....", "#....", "#...." } },
	{ 'Q', { ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#" } },
	{ 'R', { "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#" } },
	{ 'S', { ".####", "#....", "#....", ".###.", "....#", "....#", "####." } },
	{ 'T', { "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.." } },
	{ 'U', { "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###." } },
	{ 'V', { "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.." } },
	{ 'W', { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" } },
	{ 'X', { "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#" } },
	{ 'Y', { "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.." } },
	{ 'Z', { "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####" } },
	{ '0', { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." } },
	{ '1', { ".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###" } },
	{ '2', { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" } },
	{ '3', { "####.", "....#", "....#", ".###.", "....#", "....#", "####." } },
	{ '4', { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." } },
	{ '5', { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." } },
	{ '6', { ".###.", "#....", "#....", "####.", "#...#", "#...#", ".###." } },
	{ '7', { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." } },
	{ '8', { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." } },
	{ '9', { ".###.", "#...#", "#...#", ".####", "....#", "....#", ".###." } },
	{ '-', { "...", "...", "...", "###", "...", "...", "..." } },
	{ '&', { ".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#" } },
	{ '!', { "#", "#", "#", "#", "#", ".", "#" } },
	{ '.', { ".", ".", ".", ".", ".", ".", "#" } },
	{ '\'', { "#", "#", ".", ".", ".", ".", "." } },
	{ ' ', { "..", "..", "..", "..", "..", "..", ".." } },
};

static const glyph *find(char c)
{
	size_t i;
	c = (char)toupper((unsigned char)c);
	for (i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++)
		if (GLYPHS[i].c == c) return &GLYPHS[i];
	return NULL;
}

int pf_has(char c) { return find(c) != NULL; }

/* The words, with a long one broken where its capitals say ("TurboGrafx") */
static int words_of(const char *name, char w[16][32])
{
	int n = 0;
	const char *p = name;

	while (*p && n < 16) {
		char cur[32];
		int k = 0, j, brk = -1;

		while (*p == ' ') p++;
		if (!*p) break;
		while (*p && *p != ' ' && k < (int)sizeof cur - 1) cur[k++] = *p++;
		cur[k] = '\0';
		if (k > PF_LINE_MAX)
			for (j = 1; j < k; j++)
				if (isupper((unsigned char)cur[j]) && islower((unsigned char)cur[j - 1])) { brk = j; break; }
		if (brk > 0 && n < 15) {
			snprintf(w[n++], 32, "%.*s", brk, cur);
			snprintf(w[n++], 32, "%s", cur + brk);
		} else {
			snprintf(w[n++], 32, "%s", cur);
		}
	}
	return n;
}

int pf_lines(const char *name, char lines[PF_LINES_MAX][32], int *accent_from)
{
	char w[16][32];
	int nw = words_of(name, w), n = 0, i, k;

	if (accent_from) *accent_from = 0;
	for (i = 0; i < nw; i++) {
		size_t cur = n ? strlen(lines[n - 1]) : 0;
		if (n && cur + 1 + strlen(w[i]) <= PF_LINE_MAX) {
			snprintf(lines[n - 1] + cur, 32 - cur, " %.*s", 30, w[i]);
		} else {
			if (n == PF_LINES_MAX) break;
			snprintf(lines[n++], 32, "%.*s", 31, w[i]);
		}
		/* the last word starts here, in the line it landed in */
		if (accent_from) *accent_from = (int)(strlen(lines[n - 1]) - strlen(w[i]));
	}
	for (i = 0; i < n; i++)
		for (k = 0; lines[i][k]; k++) lines[i][k] = (char)toupper((unsigned char)lines[i][k]);
	return n;
}

static int line_width(const char *s)
{
	int w = 0, i;
	for (i = 0; s[i]; i++) {
		const glyph *g = find(s[i]);
		if (!g) g = find(' ');
		w += (i ? 1 : 0) + (int)strlen(g->row[0]);
	}
	return w;
}

int pf_layout(const char *name, pf_pt *pts, int max, int *cols, int *rows)
{
	char lines[PF_LINES_MAX][32];
	int acc_from, nl = pf_lines(name, lines, &acc_from), W = 0, n = 0, l, i, r, x;

	for (l = 0; l < nl; l++) if (line_width(lines[l]) > W) W = line_width(lines[l]);
	for (l = 0; l < nl; l++) {
		int x0 = (W - line_width(lines[l])) / 2, y0 = l * (PF_GLYPH_H + PF_LINE_GAP);
		for (i = 0; lines[l][i]; i++) {
			const glyph *g = find(lines[l][i]);
			int gw;
			if (!g) g = find(' ');
			gw = (int)strlen(g->row[0]);
			for (r = 0; r < PF_GLYPH_H; r++)
				for (x = 0; x < gw; x++)
					if (g->row[r][x] == '#' && n < max) {
						pts[n].x = (short)(x0 + x);
						pts[n].y = (short)(y0 + r);
						pts[n].accent = l == nl - 1 && i >= acc_from;
						n++;
					}
			x0 += gw + 1;
		}
	}
	if (cols) *cols = W;
	if (rows) *rows = nl ? nl * (PF_GLYPH_H + PF_LINE_GAP) - PF_LINE_GAP : 0;
	return n;
}
