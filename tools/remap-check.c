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
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("ok: every check passed\n");
	return 0;
}
