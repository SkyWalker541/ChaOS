/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Hardware Test's list and summary (src/hwtest.h): the stick clicks counted
 * only on a device with sticks, and the log naming every button never seen,
 * so a new device's report says what didn't reach ChaOS. */
#include "hwtest.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "hwtest-check: %s\n", m); fails++; } } while (0)

int main(void)
{
	bool seen[HWT_COUNT] = { false };
	char miss[256];
	int b;

	CHECK(hwtest_total(false) == HWT_COUNT - 2 && hwtest_total(true) == HWT_COUNT, "the totals");
	for (b = 0; b < HWT_COUNT; b++) seen[b] = true;
	seen[HWT_L3] = seen[HWT_R3] = false;
	CHECK(hwtest_summary(seen, false, miss, sizeof miss) == HWT_COUNT - 2 && !miss[0],
	      "a Brick with every button seen still asked for L3 and R3");
	CHECK(hwtest_summary(seen, true, miss, sizeof miss) == HWT_COUNT - 2 && !strcmp(miss, "L3, R3"),
	      "a Brick Pro's missing stick clicks weren't named");
	seen[HWT_F2] = false;
	hwtest_summary(seen, true, miss, sizeof miss);
	CHECK(!strcmp(miss, "L3, R3, F2"), "a missing F2 wasn't named in order");
	for (b = 0; b < HWT_COUNT; b++) seen[b] = false;
	hwtest_summary(seen, true, miss, 16);
	CHECK(strlen(miss) < 16, "the list ran past its room");
	for (b = 0; b < HWT_COUNT; b++) CHECK(HWT_NAME[b] && HWT_NAME[b][0], "a button with no name");
	if (fails) return 1;
	printf("ok: Hardware Test names what never reached ChaOS\n");
	return 0;
}
