/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Button Mapping's tables - src/remap.c - without a device. */
#include <stdio.h>
#include <string.h>
#include "../src/remap.h"

static int fails;
static void ck(int c, const char *w) { if (!c) { printf("  FAIL %s\n", w); fails++; } }

static const char *lab(const char *tag, const rm_map *m, int b)
{
	static char buf[8][24];
	static int i;
	i = (i + 1) % 8;
	return rm_choice_label(tag, m->choice[b], buf[i], sizeof buf[i]);
}

int main(void)
{
	rm_map m;
	char spec[512];

	printf("each console's buttons, by its core's names:\n");
	rm_from_spec("MD", NULL, &m);
	ck(!strcmp(lab("MD", &m, 0), "C") && !strcmp(lab("MD", &m, 1), "B") &&
	   !strcmp(lab("MD", &m, 3), "A") && !strcmp(lab("MD", &m, 2), "Y") &&
	   !strcmp(lab("MD", &m, 4), "X") && !strcmp(lab("MD", &m, 5), "Z") &&
	   !strcmp(lab("MD", &m, 8), "Mode"), "a Genesis as Genesis Plus GX names it");
	rm_from_spec("PCE", NULL, &m);
	ck(!strcmp(lab("PCE", &m, 0), "I") && !strcmp(lab("PCE", &m, 9), "Run"), "a PC Engine's I and Run");
	rm_from_spec("GB", "x:a~3,y:b~3", &m);
	ck(!strcmp(lab("GB", &m, 2), "Turbo A") && !strcmp(lab("GB", &m, 3), "Turbo B"),
	   "the Game Boy's shipped turbo reads as Turbo A and B");
	ck(!strcmp(lab("GB", &m, 4), "None"), "and a button it has none for, None");
	rm_from_spec("GBA", NULL, &m);
	ck(!strcmp(lab("GBA", &m, 6), "Turbo L") && !strcmp(lab("GBA", &m, 4), "L"),
	   "GBA keeps mGBA's own turbo L on L2");

	printf("into a SETMAP line and back:\n");
	rm_from_spec("SFC", "a:b,b:a,x:y~3,start:select,select:none", &m);
	ck(rm_to_spec("SFC", &m, spec, sizeof spec), "it fits");
	ck(!strcmp(spec, "a:b,b:a,x:y~3,y:y,l1:l1,r1:r1,l2:none,r2:none,select:none,start:select"),
	   "every button, whole, turbo with its period");
	{
		rm_map back;
		rm_from_spec("SFC", spec, &back);
		ck(!memcmp(&back, &m, sizeof m), "and read back the same");
	}
	ck(!strstr(spec, "menu"), "MENU is never in it");

	/* Turbo Speed (Trebuchet's idea): one speed for the map's turbo buttons,
	 * in the ~period Diatom already takes, so maps saved before it read as
	 * Normal and Diatom needs nothing new */
	printf("Turbo Speed:\n");
	rm_from_spec("GB", "x:a~3,y:b~3", &m);
	ck(m.period == RM_TURBO, "a map from before reads as Normal");
	ck(!strcmp(rm_speed_label(m.period, spec, sizeof spec), "Normal"), "and says so");
	rm_from_spec("GB", NULL, &m);
	ck(m.period == RM_TURBO, "no map at all is Normal");
	m.period = rm_speed_step(m.period, 1);
	ck(m.period == 2, "one step faster is Fast");
	rm_from_spec("GB", "x:a~2,y:b~2", &m);
	ck(rm_to_spec("GB", &m, spec, sizeof spec) && strstr(spec, "x:a~2") && strstr(spec, "y:b~2"),
	   "every turbo button is sent at the map's speed");
	{
		rm_map back;
		rm_from_spec("GB", spec, &back);
		ck(!memcmp(&back, &m, sizeof m), "and read back the same");
	}
	rm_from_spec("SFC", "a:b,b:a", &m);
	ck(rm_to_spec("SFC", &m, spec, sizeof spec) && !strchr(spec, '~'),
	   "a map with no turbo button carries no speed");
	ck(!rm_has_turbo("SFC", &m), "and has no turbo button, so its row rests");
	rm_from_spec("GB", "x:a~2", &m);
	ck(rm_has_turbo("GB", &m), "one turbo button wakes it");
	{
		int p = RM_TURBO, seen = 0, k;
		for (k = 0; k < RM_SPEEDS; k++) { p = rm_speed_step(p, 1); seen |= 1 << p; }
		ck(p == RM_TURBO && seen == ((1 << 1) | (1 << 2) | (1 << 3) | (1 << 5)),
		   "Right goes round all four speeds and back");
		ck(rm_speed_step(rm_speed_step(RM_TURBO, 1), -1) == RM_TURBO, "Left undoes Right");
		ck(rm_speed_step(5, 1) == 3 && rm_speed_step(1, 1) == 5, "slowest first, and round");
	}
	rm_from_spec("GB", "x:a~7", &m);
	ck(m.period == 7 && !strcmp(rm_speed_label(7, spec, sizeof spec), "Custom"),
	   "a speed no preset has is kept, and called Custom");
	ck(rm_speed_step(7, 1) == 5 && rm_speed_step(7, -1) == 1 && rm_speed_step(4, -1) == 5 &&
	   rm_speed_step(4, 1) == 3, "and steps to the nearest preset that way");
	rm_from_spec("GB", "x:a~0,y:b~31", &m);
	ck(m.period == RM_TURBO, "a speed Diatom wouldn't take reads as Normal");
	rm_from_spec("GB", "x:a~x", &m);
	ck(m.period == RM_TURBO && !strcmp(lab("GB", &m, 2), "Turbo A"), "nor does one that isn't a number");

	/* The page's rows, by what they are, in both states: from a shelf, and
	 * in a game with the For row (the rule: a row does what it says) */
	printf("Button Mapping's rows:\n");
	{
		int st;
		for (st = 0; st < 2; st++) {
			int ids[RM_ROWS_MAX], n = rm_rows(st == 1, ids, RM_ROWS_MAX), k, speed = -1, reset = -1;
			int seen[RM_BUTTONS] = { 0 }, last_btn = -1, scopes = 0, speeds = 0, ok = 1;

			for (k = 0; k < n; k++) {
				if (ids[k] >= 0 && ids[k] < RM_BUTTONS) { seen[ids[k]]++; last_btn = k; }
				else if (ids[k] == RM_ROW_SPEED) { speed = k; speeds++; }
				else if (ids[k] == RM_ROW_RESET) reset = k;
				else if (ids[k] == RM_ROW_SCOPE) scopes++;
				else if (ids[k] != RM_ROW_NONE) ok = 0;
			}
			for (k = 0; k < RM_BUTTONS; k++) if (seen[k] != 1) ok = 0;
			ck(ok, st ? "in a game: each button once, nothing unknown" : "from a shelf: each button once, nothing unknown");
			ck(speeds == 1 && speed > last_btn && reset > speed && reset == n - 1,
			   "Turbo Speed once, after the buttons, Reset last");
			ck(scopes == st && (!st || ids[0] == RM_ROW_SCOPE), "For only in a game, first");
			ck(n <= RM_ROWS_MAX, "it fits its table");
		}
	}

	/* Pressing each row moves its own setting and nothing else, with a
	 * turbo button and without (where Turbo Speed rests) */
	printf("each row, pressed:\n");
	{
		const char *maps[2] = { "x:a~3,y:b~3", "a:a,b:b" };
		int st, k;
		for (st = 0; st < 2; st++) {
			int ids[RM_ROWS_MAX], n = rm_rows(true, ids, RM_ROWS_MAX), bad = 0;
			for (k = 0; k < n; k++) {
				rm_map before, after;
				int b, moved;
				rm_from_spec("GB", maps[st], &before);
				after = before;
				moved = rm_press("GB", &after, ids[k], 1);
				for (b = 0; b < RM_BUTTONS; b++)
					if (after.choice[b] != before.choice[b] && b != ids[k]) bad++;
				if (ids[k] >= 0 && ids[k] < RM_BUTTONS && (!moved || after.choice[ids[k]] == before.choice[ids[k]] ||
				                                            after.period != before.period)) bad++;
				if (ids[k] == RM_ROW_SPEED && (moved != (st == 0) ||
				                               (st == 0 && after.period == before.period))) bad++;
				if ((ids[k] == RM_ROW_SCOPE || ids[k] == RM_ROW_RESET || ids[k] == RM_ROW_NONE) && moved) bad++;
			}
			ck(!bad, st ? "without a turbo button: each row its own, Turbo Speed nothing"
			            : "with turbo buttons: each row its own, Turbo Speed the speed");
		}
	}
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("ok: every check passed\n");
	return 0;
}
