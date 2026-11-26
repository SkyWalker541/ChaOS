/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See perf.h. */
#include "perf.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000u + (unsigned)(t.tv_nsec / 1000000));
}

bool perf_read(pid_t pid, perf_snap *out)
{
	char path[64], line[512];
	FILE *f;

	memset(out, 0, sizeof *out);
	out->ms = now_ms();
	if (pid <= 0) return false;
	snprintf(path, sizeof path, "/proc/%d/status", (int)pid);
	if (!(f = fopen(path, "r"))) return false;
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "VmRSS:", 6)) out->rss_kb = strtol(line + 6, NULL, 10);
		else if (!strncmp(line, "VmHWM:", 6)) out->hwm_kb = strtol(line + 6, NULL, 10);
	}
	fclose(f);
	snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
	if (!(f = fopen(path, "r"))) return false;
	if (fgets(line, sizeof line, f)) {
		/* after the ")" that ends the name: state is field 3, utime 14, stime 15 */
		const char *p = strrchr(line, ')');
		unsigned long ut = 0, st = 0;

		if (p && sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &st) == 2) {
			out->ticks = (unsigned long long)ut + st;
			out->ok = true;
		}
	}
	fclose(f);
	return out->ok;
}

void perf_reset_peak(pid_t pid)
{
	char path[64];
	int fd;

	if (pid <= 0) return;
	snprintf(path, sizeof path, "/proc/%d/clear_refs", (int)pid);
	if ((fd = open(path, O_WRONLY)) < 0) return;
	if (write(fd, "5", 1) != 1) { }
	close(fd);
}

int perf_cpu_pct(const perf_snap *a, const perf_snap *b)
{
	long hz = sysconf(_SC_CLK_TCK);
	unsigned ms = b->ms - a->ms;

	if (!a->ok || !b->ok || ms == 0 || hz <= 0 || b->ticks < a->ticks) return -1;
	return (int)((b->ticks - a->ticks) * 1000ull * 100ull / ((unsigned long long)hz * ms));
}

int perf_cores(void)
{
	long n = sysconf(_SC_NPROCESSORS_ONLN);

	return n > 0 ? (int)n : 1;
}

static bool read_line(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "r");
	bool ok = false;

	if (!f) return false;
	if (fgets(out, (int)n, f)) { out[strcspn(out, "\n")] = '\0'; ok = out[0] != '\0'; }
	fclose(f);
	return ok;
}

/* A devfreq device that is the GPU: its load ("37@600000000Hz") where the
 * driver keeps one, else its clock. The Brick's kernel may report neither,
 * and then it says so rather than guess. */
void perf_gpu(char *out, size_t n)
{
	DIR *d = opendir("/sys/class/devfreq");
	struct dirent *e;

	snprintf(out, n, "not reported");
	if (!d) return;
	while ((e = readdir(d))) {
		char p[320], v[64], name[64];

		if (e->d_name[0] == '.') continue;
		snprintf(p, sizeof p, "/sys/class/devfreq/%s/name", e->d_name);
		if (!read_line(p, name, sizeof name)) snprintf(name, sizeof name, "%.63s", e->d_name);
		if (!strstr(name, "gpu") && !strstr(name, "GPU") && !strstr(e->d_name, "gpu")) continue;
		snprintf(p, sizeof p, "/sys/class/devfreq/%s/load", e->d_name);
		if (read_line(p, v, sizeof v)) {
			int load = atoi(v);
			const char *at = strchr(v, '@');

			snprintf(out, n, "load %d%%%s%s", load, at ? " at " : "", at ? at + 1 : "");
			break;
		}
		snprintf(p, sizeof p, "/sys/class/devfreq/%s/cur_freq", e->d_name);
		if (read_line(p, v, sizeof v)) {
			snprintf(out, n, "clock %ld MHz", atol(v) / 1000000);
			break;
		}
	}
	closedir(d);
}
