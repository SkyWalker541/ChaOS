/* ChaOS: the pixel names' layout (src/pixfont.c), for every shelf name. */
#include <stdio.h>
#include <string.h>

#include "../src/pixfont.h"

static int fails;
static void ck(int c, const char *what) { if (!c) { printf("  FAIL %s\n", what); fails++; } }

int main(void)
{
	static const char *names[] = {
		"NES", "Master System", "Game Boy", "Genesis", "TurboGrafx-16", "Game Gear",
		"SNES", "Neo Geo Pocket", "Game Boy Color", "Neo Geo Pocket Color",
		"Game Boy Advance", "Favorites", "Muse",
	};
	static pf_pt pts[PF_PTS_MAX];
	char lines[PF_LINES_MAX][32], msg[96];
	int i, n, cols, rows, acc, l, k, accented;

	printf("pixel names:\n");
	for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++) {
		n = pf_lines(names[i], lines, &acc);
		for (l = 0; l < n; l++) {
			snprintf(msg, sizeof msg, "%s: \"%s\" fits a line", names[i], lines[l]);
			ck((int)strlen(lines[l]) <= PF_LINE_MAX, msg);
			for (k = 0; lines[l][k]; k++) {
				snprintf(msg, sizeof msg, "%s: '%c' has a glyph", names[i], lines[l][k]);
				ck(pf_has(lines[l][k]), msg);
			}
		}
		n = pf_layout(names[i], pts, PF_PTS_MAX, &cols, &rows);
		for (k = accented = 0; k < n; k++) accented += pts[k].accent;
		snprintf(msg, sizeof msg, "%s: drawn, %d pixels, its last word in the accent", names[i], n);
		ck(n > 0 && accented > 0 && accented < n + (strchr(names[i], ' ') ? 0 : 1), msg);
		snprintf(msg, sizeof msg, "%s: %dx%d fits the card's 53 columns", names[i], cols, rows);
		ck(cols <= 53 && rows <= 4 * 9, msg);
	}
	n = pf_lines("TurboGrafx-16", lines, NULL);
	ck(n == 2 && !strcmp(lines[0], "TURBO") && !strcmp(lines[1], "GRAFX-16"),
	   "TurboGrafx-16 breaks where its capitals do");
	n = pf_lines("Neo Geo Pocket Color", lines, NULL);
	ck(n == 3 && !strcmp(lines[0], "NEO GEO") && !strcmp(lines[2], "COLOR"),
	   "Neo Geo Pocket Color is three lines");
	n = pf_lines("Game Boy", lines, &acc);
	ck(n == 1 && !strcmp(lines[0], "GAME BOY") && acc == 5, "Game Boy is one line, BOY in the accent");
	printf(fails ? "FAIL\n" : "\nok: every name fits, in the logo's letters\n");
	return fails != 0;
}
