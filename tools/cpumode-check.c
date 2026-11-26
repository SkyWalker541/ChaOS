/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Performance's modes (src/cpumode.h): what each sets on the Brick, that
 * Auto on a game changes nothing and on a port is what ports had before,
 * and that left and right go round the choices without skipping or
 * leaving them. */
#include "cpumode.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "cpumode-check: %s\n", m); fails++; } } while (0)

static int plan_is(int m, bool port, long now_max, long wl, long wt)
{
	long lo = -1, top = -1;

	return cpumode_plan(m, port, 408000, 2000000, now_max, &lo, &top) && lo == wl && top == wt;
}

int main(void)
{
	long lo, top;
	int m, seen, i;

	/* The Brick: 408 to 2000 MHz, as cpuinfo says */
	CHECK(!cpumode_plan(CPU_AUTO, false, 408000, 2000000, 2000000, &lo, &top),
	      "Auto on a game changed the clock");
	CHECK(plan_is(CPU_AUTO, true, 2000000, 1608000, 2000000), "a port's Auto isn't 1608-2000 as before");
	CHECK(plan_is(CPU_AUTO, true, 1800000, 1608000, 1800000), "a port's Auto went past the top it had");
	CHECK(plan_is(CPU_POWERSAVE, false, 2000000, 408000, 1488000), "Powersave isn't 408-1488");
	CHECK(plan_is(CPU_BALANCED, true, 2000000, 1008000, 1800000), "Balanced isn't 1008-1800");
	CHECK(plan_is(CPU_PERFORMANCE, false, 2000000, 1800000, 1800000), "Performance isn't 1800 fixed");
	CHECK(plan_is(CPU_MAX, false, 2000000, 2000000, 2000000), "Max isn't the top, fixed");
	/* A device whose top is lower: nothing set above it, no floor over the top */
	CHECK(cpumode_plan(CPU_PERFORMANCE, false, 408000, 1200000, 1200000, &lo, &top) &&
	      lo == 1200000 && top == 1200000, "Performance went past a lower top");
	CHECK(!cpumode_plan(CPU_MAX, false, 408000, 0, 0, &lo, &top), "a device with no top was changed");

	/* A console's go round five, a game's round Default and the five */
	for (m = CPU_AUTO, seen = 0, i = 0; i < CPU_MODES; i++) { seen |= 1 << m; m = cpumode_step(m, 1, false); }
	CHECK(m == CPU_AUTO && seen == (1 << CPU_MODES) - 1, "a console's choices skip or don't come round");
	CHECK(cpumode_step(CPU_AUTO, -1, false) == CPU_MAX, "left from Auto isn't Max");
	CHECK(cpumode_step(CPU_MAX, 1, true) == CPU_DEFAULT, "a game's right from Max isn't Default");
	CHECK(cpumode_step(CPU_DEFAULT, -1, true) == CPU_MAX, "a game's left from Default isn't Max");
	CHECK(cpumode_step(CPU_DEFAULT, 1, false) == CPU_POWERSAVE || cpumode_step(CPU_DEFAULT, 1, false) == CPU_AUTO + 1,
	      "a console's step from a game's Default isn't in its choices");
	CHECK(!strcmp(cpumode_name(CPU_DEFAULT), "Default") && !strcmp(cpumode_name(CPU_MAX), "Max") &&
	      !strcmp(cpumode_name(99), "Auto"), "the names");

	if (fails) return 1;
	printf("ok: Performance's modes set what they say\n");
	return 0;
}
