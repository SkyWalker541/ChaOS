/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Logging > Performance's readings (src/perf.c), on this machine's
 * own /proc: added with it in 2.7.9-test2. A process that spins reads near
 * 100% of one core and one that sleeps near 0; its RAM and peak are there;
 * a process that isn't there reads nothing rather than zeros. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../src/perf.h"

static int fails;
#define CK(c, ...) do { if (!(c)) { fails++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	perf_snap a, b;
	volatile unsigned long x = 0;
	struct timespec t0, t;
	char gpu[64];
	int pct;

	printf("perf: what Logging > Performance reads\n");
	CK(perf_read(getpid(), &a), "this process can be read");
	CK(a.rss_kb > 0 && a.hwm_kb >= a.rss_kb, "its RAM %ld KB, peak %ld KB", a.rss_kb, a.hwm_kb);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	do { x++; clock_gettime(CLOCK_MONOTONIC, &t); }
	while ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000 < 400);
	perf_read(getpid(), &b);
	pct = perf_cpu_pct(&a, &b);
	CK(pct >= 60 && pct <= 110, "spinning reads near one core: %d%%", pct);
	a = b;
	usleep(400 * 1000);
	perf_read(getpid(), &b);
	pct = perf_cpu_pct(&a, &b);
	CK(pct >= 0 && pct <= 15, "sleeping reads near nothing: %d%%", pct);
	CK(!perf_read(999999, &a) && !a.ok, "a process that isn't there reads nothing");
	CK(perf_cpu_pct(&a, &b) == -1, "and gives no percentage");
	perf_reset_peak(getpid());
	perf_gpu(gpu, sizeof gpu);
	CK(gpu[0], "the GPU says something: %s", gpu);
	CK(perf_cores() >= 1, "cores");
	if (fails) { printf("perf: %d failed\n", fails); return 1; }
	printf("ok: RAM, CPU and GPU read (GPU here: %s)\n", gpu);
	return 0;
}
