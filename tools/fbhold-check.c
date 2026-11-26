/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Power off's logo in every page of the framebuffer (src/fbhold.h).
 *
 * What broke: the logo was presented once, into one of the two pages the GPU
 * flips between, and the launcher's exit left the panel on the other page
 * half the time - the shelf flashed back over the logo until the power went
 * (2.7.9-test5 to test7). This checks that the copy fills every page, row by
 * row on a stride wider than the frame, with every pixel opaque, and that a
 * frame that doesn't fit writes nothing. */
#include "fbhold.h"

#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "fbhold-check: %s\n", m); fails++; } } while (0)

int main(void)
{
	enum { W = 8, H = 5, PAGES = 3, LINE = W * 4 + 16, PITCH = W * 4 + 4 };
	uint8_t src[PITCH * H], *fb = malloc((size_t)LINE * H * PAGES);
	int p, y, x;

	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			uint32_t v = (uint32_t)(y * 16 + x) * 0x010203u;   /* alpha 0 on purpose */
			memcpy(src + y * PITCH + x * 4, &v, 4);
		}
	memset(fb, 0x55, (size_t)LINE * H * PAGES);

	CHECK(fbhold_fill(fb, PAGES, LINE, src, PITCH, W, H), "a frame that fits was refused");
	for (p = 0; p < PAGES; p++)
		for (y = 0; y < H; y++) {
			const uint8_t *row = fb + ((size_t)p * H + y) * LINE;

			for (x = 0; x < W; x++) {
				uint32_t got, want;

				memcpy(&got, row + x * 4, 4);
				memcpy(&want, src + y * PITCH + x * 4, 4);
				CHECK(got == (want | 0xFF000000u), "a page's pixel is not the frame's, opaque");
			}
			/* the stride's padding past the frame left as it was */
			CHECK(row[W * 4] == 0x55 && row[LINE - 1] == 0x55, "the stride's padding was written");
		}

	memset(fb, 0x55, (size_t)LINE * H * PAGES);
	CHECK(!fbhold_fill(fb, PAGES, W * 4 - 4, src, PITCH, W, H), "a frame wider than a page was taken");
	CHECK(fb[0] == 0x55, "a refused frame wrote into the framebuffer");
	CHECK(!fbhold_fill(fb, 0, LINE, src, PITCH, W, H), "no pages was taken");
	CHECK(!fbhold_fill(NULL, PAGES, LINE, src, PITCH, W, H), "no framebuffer was taken");

	free(fb);
	if (fails) return 1;
	printf("ok: power off's logo goes into every page, opaque\n");
	return 0;
}
