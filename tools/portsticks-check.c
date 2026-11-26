/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Brick Pro's sticks for ports (src/portsticks.h).
 *
 * What broke: run.sh's pad for ports had buttons only, so a port on a Brick
 * Pro saw no sticks, and the axes as brick-pad reports them (0-4095, the
 * stick resting off the middle, ~850 each way) would have read as a drifting
 * stick that never reaches its end. This checks the mapping follows
 * Calibrate Sticks (which raw axis, which way), SDL's numbering, and that
 * each range is centred on the stick's rest, as far as its shorter side. */
#include "portsticks.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "portsticks-check: %s\n", m); fails++; } } while (0)

int main(void)
{
	/* lx on raw 0, ly on raw 1 reversed (up is the higher value), rx and ry
	 * swapped onto raws 3 and 2 */
	portsticks_cal cal[4] = {
		{ 0, 1200, 2050, 2900 },
		{ 1, 2950, 2100, 1300 },
		{ 3, 1100, 1950, 2700 },
		{ 2, 1150, 2000, 2850 },
	};
	int sdl[4] = { 0, 1, 3, 4 };   /* an ABS_Z between Y and RX makes RX a3 */
	portsticks_range rg[4];
	int set[4];
	char map[128];

	CHECK(portsticks_build(cal, sdl, map, sizeof map, rg, set) == 4, "not every axis was mapped");
	CHECK(!strcmp(map, "leftx:a0,lefty:a1~,rightx:a4,righty:a3,"), map);
	CHECK(set[0] && set[1] && set[2] && set[3], "a range was left out");
	CHECK(rg[0].min == 2050 - 850 && rg[0].max == 2050 + 850, "lx not centred on its rest");
	/* ly: 850 one way, 800 the other - the shorter, so both ends reach 1 */
	CHECK(rg[1].min == 2100 - 800 && rg[1].max == 2100 + 800, "ly not its shorter side");
	CHECK(rg[3].min == 1950 - 750 && rg[3].max == 1950 + 750, "rx's range not on its own raw axis");
	CHECK(rg[0].flat > 0 && rg[0].flat < 850 / 4, "lx's flat zone not small");

	/* An axis SDL won't number, or one that barely moves, isn't a stick */
	{
		int sdl2[4] = { 0, -1, 2, 3 };
		portsticks_cal c2[4] = { cal[0], cal[1], { 3, 1940, 1950, 1960 }, cal[3] };

		CHECK(portsticks_build(c2, sdl2, map, sizeof map, rg, set) == 2, "a missing or still axis was mapped");
		CHECK(!strcmp(map, "leftx:a0,righty:a2,"), map);
		CHECK(!set[1] && !set[3], "a range was set for an axis left out");
	}
	/* Two sticks on one raw axis (a bad sticks.cfg): the first only */
	{
		portsticks_cal c3[4] = { cal[0], { 0, 1200, 2050, 2900 }, cal[2], cal[3] };

		CHECK(portsticks_build(c3, sdl, map, sizeof map, rg, set) == 3, "one raw axis was mapped twice");
	}
	/* Too little room: nothing half written */
	CHECK(portsticks_build(cal, sdl, map, 12, rg, set) <= 1 && strlen(map) < 12, "the mapping overran");

	if (fails) return 1;
	printf("ok: the Brick Pro's sticks reach ports, calibrated\n");
	return 0;
}
