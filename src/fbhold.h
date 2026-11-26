/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: power off's last frame copied into every page of the framebuffer.
 *
 * The panel scans one page of fb0 at a time, and fb0 has three. The launcher
 * draws through the GPU, which flips between two of them (Diatom measured
 * this: port/brick.c, at its park page). Power off presented the logo once,
 * so one page held the logo and the page beside it the shelf from the frame
 * before. When the launcher exits, the GPU's driver lets go of its pages and
 * the panel is left on one of its choosing: the logo stayed when that was the
 * logo's page, and the shelf flashed back when it was the other, until the
 * power went - "the shutdown image sometimes works, and sometimes flashes and
 * disappears" (2.7.9-test5 to test7, 2026-11-26). With the logo in every page,
 * whichever page the panel ends on is the logo.
 *
 * No SDL here, so tools/fbhold-check.c can test the copy on a build machine. */
#ifndef CHAOS_FBHOLD_H
#define CHAOS_FBHOLD_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The frame `src` (w x h, 32-bit ARGB, `pitch` bytes a row) into each of
 * `pages` pages at `dst`, each `line` bytes a row and `h` rows. Every pixel
 * opaque: a zero alpha byte makes a pixel invisible on this panel rather than
 * black (Diatom's g_opaque, 2026-08-24). False, and nothing written, when the
 * frame doesn't fit a page. */
static inline int fbhold_fill(uint8_t *dst, int pages, size_t line,
                              const uint8_t *src, size_t pitch, int w, int h)
{
	int p, y, x;

	if (!dst || !src || pages < 1 || w < 1 || h < 1 ||
	    line < (size_t)w * 4 || pitch < (size_t)w * 4)
		return 0;
	for (p = 0; p < pages; p++) {
		uint8_t *page = dst + (size_t)p * line * (size_t)h;

		for (y = 0; y < h; y++) {
			uint32_t *d = (uint32_t *)(page + (size_t)y * line);
			const uint8_t *s = src + (size_t)y * pitch;

			memcpy(d, s, (size_t)w * 4);
			for (x = 0; x < w; x++) d[x] |= 0xFF000000u;
		}
	}
	return 1;
}

#endif
