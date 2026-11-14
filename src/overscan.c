/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Crop Overscan - see overscan.h */
#include "overscan.h"

#include <string.h>

static const os_console CONSOLES[] = {
	/* FCEUmm crops 8 lines top and bottom by default: Top & Bottom is how
	 * the NES has always looked here. All Sides takes the 8-pixel columns too,
	 * where scrolling games leave their seams. */
	{ "NES", 1, 3, {
		{ "Off",          { "fceumm_overscan_v_top", "fceumm_overscan_v_bottom",
		                    "fceumm_overscan_h_left", "fceumm_overscan_h_right" },
		                  { "0", "0", "0", "0" } },
		{ "Top & Bottom", { "fceumm_overscan_v_top", "fceumm_overscan_v_bottom",
		                    "fceumm_overscan_h_left", "fceumm_overscan_h_right" },
		                  { "8", "8", "0", "0" } },
		{ "All Sides",    { "fceumm_overscan_v_top", "fceumm_overscan_v_bottom",
		                    "fceumm_overscan_h_left", "fceumm_overscan_h_right" },
		                  { "8", "8", "8", "8" } },
	} },
	/* Beetle PCE Fast shows lines 3 to 242; a television showed about 224 */
	{ "PCE", 0, 2, {
		{ "Off", { "pce_fast_initial_scanline", "pce_fast_last_scanline" }, { "3", "242" } },
		{ "On",  { "pce_fast_initial_scanline", "pce_fast_last_scanline" }, { "11", "234" } },
	} },
	/* The Master System blanks its left column in many games; Genesis Plus GX
	 * can take it off, or both sides to keep the picture centered */
	{ "SMS", 0, 3, {
		{ "Off",        { "genesis_plus_gx_left_border" }, { "disabled" } },
		{ "Left",       { "genesis_plus_gx_left_border" }, { "left border" } },
		{ "Both Sides", { "genesis_plus_gx_left_border" }, { "left & right borders" } },
	} },
};

const os_console *os_for(const char *tag)
{
	size_t i;

	if (!tag) return NULL;
	for (i = 0; i < sizeof CONSOLES / sizeof CONSOLES[0]; i++)
		if (!strcmp(CONSOLES[i].tag, tag)) return &CONSOLES[i];
	return NULL;
}

int os_current(const os_console *c, const char *(*get)(const char *key, void *ctx), void *ctx)
{
	int i, k;

	for (i = 0; i < c->n; i++) {
		bool all = true;

		for (k = 0; k < OS_KEYS && c->c[i].key[k]; k++) {
			const char *v = get(c->c[i].key[k], ctx);
			if (!v || strcmp(v, c->c[i].value[k])) { all = false; break; }
		}
		if (all) return i;
	}
	return c->def;
}
