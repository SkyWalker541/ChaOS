/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: the Brick Pro's sticks for ports, as PortMaster expects them.
 *
 * Ports read the pad through SDL's game controller, laid out by run.sh's
 * CHAOS_PAD. That had buttons only, so a port on a Brick Pro saw no sticks at
 * all. Two things make the sticks usable there:
 *
 * - The mapping: which of SDL's axes is leftx, lefty, rightx and righty, and
 *   which way each runs. Calibrate Sticks (sticks.cfg) says which raw axis is
 *   which and which end is which; SDL numbers a device's axes in the order of
 *   their ABS codes, hats left out, so the number is worked out from the
 *   device's own axes, and a reversed axis gets SDL's "~".
 * - The range: brick-pad reports each axis as 0 to 4095, and SDL scales from
 *   the kernel's minimum and maximum with the middle between them. A Brick
 *   Pro's stick rests off that middle and travels about 850 each way, so as
 *   reported a port would see a stick that drifts and never reaches its end.
 *   For the port, each axis's range is set around its calibrated middle, as
 *   far each way as its shorter side, with a small flat zone at rest; the
 *   values themselves are untouched, and the range is put back after.
 *
 * No SDL or kernel headers here, so tools/portsticks-check.c can test it. */
#ifndef CHAOS_PORTSTICKS_H
#define CHAOS_PORTSTICKS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int idx, neg, mid, pos; } portsticks_cal;   /* plat_stick_cal */
typedef struct { int min, max, flat; } portsticks_range;

/* An axis that travels less than this from rest is not taken as a stick */
#define PORTSTICKS_MIN_SPAN 100

/* For the four sticks' calibrations (lx, ly, rx, ry, as sticks.cfg), with
 * sdl_axis[r] SDL's number for raw axis r (-1 when SDL won't have it): the
 * mapping's fields into `out` ("leftx:a0,lefty:a1~," and so on, each ending
 * in a comma, empty when none), and each raw axis's range into `range`
 * (set[r] true for those to change). The number of sticks' axes mapped. */
static inline int portsticks_build(const portsticks_cal cal[4], const int sdl_axis[4],
                                   char *out, size_t n, portsticks_range range[4],
                                   int set[4])
{
	static const char *const field[4] = { "leftx", "lefty", "rightx", "righty" };
	size_t used = 0;
	int i, mapped = 0;

	if (n) out[0] = '\0';
	for (i = 0; i < 4; i++) set[i] = 0;
	for (i = 0; i < 4; i++) {
		const portsticks_cal *c = &cal[i];
		int r = c->idx, up, down, span, w;

		if (r < 0 || r > 3 || sdl_axis[r] < 0 || set[r]) continue;
		up = abs(c->pos - c->mid);
		down = abs(c->neg - c->mid);
		span = up < down ? up : down;
		if (span < PORTSTICKS_MIN_SPAN) continue;
		w = snprintf(out + used, n - used, "%s:a%d%s,", field[i], sdl_axis[r],
		             c->pos < c->mid ? "~" : "");
		if (w < 0 || (size_t)w >= n - used) { out[used] = '\0'; break; }
		used += (size_t)w;
		range[r].min = c->mid - span;
		range[r].max = c->mid + span;
		range[r].flat = span / 12;
		set[r] = 1;
		mapped++;
	}
	return mapped;
}

#endif
