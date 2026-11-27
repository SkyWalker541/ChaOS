/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Hardware Test (MENU > System Settings > Button Test, Screen Test).
 *
 * Every button the device has, by the name a player reads, so a new device -
 * the Brick Pro first - can be tried press by press and the log says which
 * reached ChaOS and which never did. The stick clicks and the sticks only on
 * a device with sticks. The list and its summary have no SDL in them, for
 * tools/hwtest-check.c. */
#ifndef CHAOS_HWTEST_H
#define CHAOS_HWTEST_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *name; int in; bool sticks_only; } hwtest_button;

/* in: the platform's in_button, filled in by the caller's table (main.c),
 * so this header needs none of it */
enum { HWT_UP, HWT_DOWN, HWT_LEFT, HWT_RIGHT, HWT_A, HWT_B, HWT_X, HWT_Y,
       HWT_L1, HWT_R1, HWT_L2, HWT_R2, HWT_L3, HWT_R3,
       HWT_SELECT, HWT_START, HWT_MENU, HWT_F1, HWT_F2,
       HWT_VOLUP, HWT_VOLDN, HWT_BRIGHTUP, HWT_BRIGHTDN, HWT_POWER, HWT_COUNT };

static const char *const HWT_NAME[HWT_COUNT] = {
	"Up", "Down", "Left", "Right", "A", "B", "X", "Y",
	"L1", "R1", "L2", "R2", "L3", "R3",
	"Select", "Start", "Menu", "F1", "F2",
	"Vol +", "Vol -", "Bright +", "Bright -", "Power"
};

static inline bool hwtest_sticks_only(int b) { return b == HWT_L3 || b == HWT_R3; }

/* How many of the device's buttons were seen, and the names of those that
 * weren't, comma separated, into `missing` */
static inline int hwtest_summary(const bool seen[HWT_COUNT], bool sticks, char *missing, size_t n)
{
	int b, got = 0, total = 0;
	size_t used = 0;

	if (n) missing[0] = '\0';
	for (b = 0; b < HWT_COUNT; b++) {
		if (hwtest_sticks_only(b) && !sticks) continue;
		total++;
		if (seen[b]) { got++; continue; }
		if (used < n) {
			int w = snprintf(missing + used, n - used, "%s%s", used ? ", " : "", HWT_NAME[b]);
			if (w > 0) used += (size_t)w < n - used ? (size_t)w : n - used - 1;
		}
	}
	(void)total;
	return got;
}

static inline int hwtest_total(bool sticks)
{
	return sticks ? HWT_COUNT : HWT_COUNT - 2;
}

#endif
