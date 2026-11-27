/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Controls Layout for ports, how SDL's a, b, x and y sit on the
 * Brick's buttons. Xbox, the default, is by position as PortMaster lays out
 * every device: the bottom button (B on the Brick) is SDL's a. Nintendo is
 * by label: A is a. Set for all ports (MENU on the Ports shelf) and per port
 * (X on it, Default following the shelf), as spruceOS's Controller Mode is
 * for its ports. PortMaster's own card is always Nintendo, so its menus
 * take A to choose and B to go back, as ChaOS's do. run.sh lays the pad out
 * from CHAOS_PAD_LAYOUT. No SDL here, for tools/menu-check.c. */
#ifndef CHAOS_PADLAYOUT_H
#define CHAOS_PADLAYOUT_H

#include <stdbool.h>

enum { PAD_DEFAULT = -1, PAD_XBOX, PAD_NINTENDO, PAD_LAYOUTS };

static inline const char *padlayout_name(int m)
{
	return m == PAD_DEFAULT ? "Default" : m == PAD_NINTENDO ? "Nintendo" : "Xbox";
}

/* The next choice: the shelf's go between the two, a port's round Default
 * and the two */
static inline int padlayout_step(int m, int d, bool port)
{
	int lo = port ? PAD_DEFAULT : PAD_XBOX, span = PAD_LAYOUTS - lo;

	if (m < lo || m >= PAD_LAYOUTS) m = lo;
	return lo + ((m - lo + (d < 0 ? -1 : 1)) % span + span) % span;
}

#endif
