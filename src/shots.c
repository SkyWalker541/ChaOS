/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: screenshots, per game. See shots.h. */
#include "shots.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int shots_dir(const char *card, const char *folder, const char *rom,
              char *out, size_t n)
{
	const char *base = strrchr(rom, '/'), *dot;
	int len, k;

	base = base ? base + 1 : rom;
	dot = strrchr(base, '.');
	len = dot && dot != base ? (int)(dot - base) : (int)strlen(base);
	k = snprintf(out, n, "%s/Screenshots/%s/%.*s", card, folder, len, base);
	return k > 0 && (size_t)k < n;
}

static int is_shot(const char *name)
{
	size_t l = strlen(name);
	return name[0] != '.' && l > 4 && l < SHOTS_NAME && !strcasecmp(name + l - 4, ".png");
}

static int newest_first(const void *a, const void *b)
{
	return strcmp((const char *)b, (const char *)a);
}

int shots_list(const char *dir, char (*names)[SHOTS_NAME], int max)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;

	if (!d) return 0;
	while ((e = readdir(d)) && n < max)
		if (is_shot(e->d_name)) {
			memcpy(names[n], e->d_name, strlen(e->d_name) + 1);   /* is_shot: it fits */
			n++;
		}
	closedir(d);
	qsort(names, (size_t)n, SHOTS_NAME, newest_first);
	return n;
}

/* mkdir -p, for the two or three levels under the card */
static int make_dirs(const char *path)
{
	char p[1024], *s;

	if (snprintf(p, sizeof p, "%s", path) >= (int)sizeof p) return 0;
	for (s = p + 1; *s; s++)
		if (*s == '/') {
			*s = '\0';
			if (mkdir(p, 0755) != 0 && errno != EEXIST) return 0;
			*s = '/';
		}
	return mkdir(p, 0755) == 0 || errno == EEXIST;
}

int shots_new_path(const char *dir, time_t t, char *out, size_t n)
{
	struct tm tm;
	char stamp[32];
	struct stat st;
	int i, k;

	if (!make_dirs(dir)) return 0;
	localtime_r(&t, &tm);
	strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", &tm);
	for (i = 1; i < 100; i++) {
		k = i == 1 ? snprintf(out, n, "%s/%s.png", dir, stamp)
		           : snprintf(out, n, "%s/%s-%d.png", dir, stamp, i);
		if (k <= 0 || (size_t)k >= n) return 0;
		if (stat(out, &st) != 0) return 1;
	}
	return 0;
}

int shots_delete(const char *dir, const char *name)
{
	char p[1100], *slash;

	if (!name || !*name || strchr(name, '/')) return 0;
	snprintf(p, sizeof p, "%s/%s", dir, name);
	if (unlink(p) != 0) return 0;
	/* the game's folder, then its console's, when nothing is left in them;
	 * rmdir refuses a folder that is not empty, which is the test */
	snprintf(p, sizeof p, "%s", dir);
	if (rmdir(p) == 0 && (slash = strrchr(p, '/'))) {
		*slash = '\0';
		rmdir(p);
	}
	return 1;
}

void shots_label(const char *name, char *out, size_t n)
{
	static const char *const MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	int y, mo, d, h, mi, s;

	if (sscanf(name, "%4d-%2d-%2d %2d.%2d.%2d", &y, &mo, &d, &h, &mi, &s) == 6 &&
	    mo >= 1 && mo <= 12 && h >= 0 && h < 24) {
		int h12 = h % 12 ? h % 12 : 12;
		snprintf(out, n, "%s %d, %d:%02d %s", MON[mo - 1], d, h12, mi, h < 12 ? "AM" : "PM");
		return;
	}
	snprintf(out, n, "%s", name);
}
