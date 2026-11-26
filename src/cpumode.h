/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Performance, how fast the CPU runs while a game or a port plays.
 *
 * Set per console (MENU on its shelf, Ports included) and per game or port
 * (X on it), which follows its console's unless it says otherwise. Auto,
 * everywhere by default, is what ChaOS did before there was a choice: a game
 * runs on TortOS's ondemand governor as the boot tuned it, from the lowest
 * clock to the top, and a port on the same with a floor of 1608 MHz.
 *
 * The other four are spruceOS's for the Brick, credited in CREDITS.md
 * (https://github.com/spruceUI/spruceOS: its Powersave, Smart, Performance
 * and Overclock, spruce/scripts/platform/tina-stock/Brick.cfg), less what doesn't suit
 * ChaOS: no core is taken offline, as that is slow and risks suspend, and
 * the governor stays ondemand throughout, a fixed clock being its floor and
 * top made one, which is how Diatom's fast-forward holds the top already.
 *
 * Set once when the game starts and put back when it ends: nothing runs in
 * between. No SDL here, so tools/cpumode-check.c can test it. */
#ifndef CHAOS_CPUMODE_H
#define CHAOS_CPUMODE_H

#include <stdbool.h>

enum {
	CPU_DEFAULT = -1,   /* a game's own: its console's */
	CPU_AUTO,
	CPU_POWERSAVE,
	CPU_BALANCED,
	CPU_PERFORMANCE,
	CPU_MAX,
	CPU_MODES
};

static inline const char *cpumode_name(int m)
{
	static const char *const names[CPU_MODES] = {
		"Auto", "Powersave", "Balanced", "Performance", "Max"
	};

	if (m == CPU_DEFAULT) return "Default";
	return m >= 0 && m < CPU_MODES ? names[m] : names[CPU_AUTO];
}

/* The next choice, `d` (-1 or 1) along: a console's go round the five, a
 * game's round Default and the five */
static inline int cpumode_step(int m, int d, bool game)
{
	int lo = game ? CPU_DEFAULT : CPU_AUTO, span = CPU_MODES - lo;

	if (m < lo || m >= CPU_MODES) m = lo;
	return lo + ((m - lo + (d < 0 ? -1 : 1)) % span + span) % span;
}

/* What `m` sets, in kHz, for a game or a port: the floor and the top, from
 * the device's own lowest and highest (cpuinfo) and the top it has now.
 * False for Auto on a game, which changes nothing. */
static inline bool cpumode_plan(int m, bool port, long hw_min, long hw_max, long now_max,
                                long *lo, long *top)
{
	long l, t;

	if (hw_max <= 0) return false;
	switch (m) {
	case CPU_POWERSAVE:   l = hw_min;  t = 1488000; break;
	case CPU_BALANCED:    l = 1008000; t = 1800000; break;
	case CPU_PERFORMANCE: l = 1800000; t = 1800000; break;
	case CPU_MAX:         l = hw_max;  t = hw_max;  break;
	default:
		if (!port) return false;
		/* a port's Auto, as before: never below the top it already had (the
		 * Brick's own reaches 2000 MHz, 2.7.9-test5) */
		l = 1608000;
		t = 1800000;
		if (t > hw_max) t = hw_max;
		if (now_max > t) t = now_max;
		break;
	}
	if (t > hw_max) t = hw_max;
	if (l > t) l = t;
	if (l < hw_min) l = hw_min;
	*lo = l;
	*top = t;
	return true;
}

#endif
