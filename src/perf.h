/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_PERF_H
#define CHAOS_PERF_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* ChaOS: Logging > Performance. An emulator's memory, processor and graphics
 * use, written to the log at the start and the end of a game and never
 * during one: everything here reads /proc and /sys once, at those two
 * moments, so a game runs exactly as it would with it off.
 *
 * Diatom stays running between games, so its "idle" is real: what it holds
 * and uses with no game. A standalone emulator (a port, PICO-8) isn't
 * running before its game, so its idle is "off", and its use is what the
 * kernel counted for the process when it ended. */
typedef struct {
	long     rss_kb, hwm_kb;   /* resident now, and the peak since reset */
	unsigned long long ticks;  /* user + system CPU time, in clock ticks */
	unsigned ms;               /* when, on a monotonic clock */
	bool     ok;
} perf_snap;

bool perf_read(pid_t pid, perf_snap *out);
/* The peak (VmHWM) starts again from now, so one game's peak is its own */
void perf_reset_peak(pid_t pid);
/* Average CPU between two snapshots, as % of one core (100 = one core busy) */
int  perf_cpu_pct(const perf_snap *a, const perf_snap *b);
/* The graphics processor as the kernel reports it, or "not reported" */
void perf_gpu(char *out, size_t n);
/* How many cores, for "of N cores" */
int  perf_cores(void);

#endif
