/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Game Boy games into their folders - see gbsort.h */
#include "gbsort.h"
#include "gamecore.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

const char *gbs_home(int flag) { return flag & 0x80 ? GBS_GBC : GBS_GB; }

static bool gb_file(const char *n)
{
	const char *dot = strrchr(n, '.');

	if (!dot || n[0] == '.') return false;
	return !strcasecmp(dot, ".gb") || !strcasecmp(dot, ".gbc") || !strcasecmp(dot, ".cgb") ||
	       !strcasecmp(dot, ".dmg") || !strcasecmp(dot, ".zip");
}

/* ---- the cache: folder \t file \t size \t mtime \t flag ---- */

typedef struct { char folder[16]; char file[256]; long long size; long long mtime; int flag; } centry;
static centry *g_c;
static int g_cn, g_ccap;

static void cache_load(const char *path)
{
	FILE *f;
	char line[600];

	g_cn = 0;
	if (!path || !(f = fopen(path, "r"))) return;
	while (fgets(line, sizeof line, f)) {
		centry e;
		char *t[5], *s = line;
		int k;

		line[strcspn(line, "\r\n")] = '\0';
		for (k = 0; k < 5; k++) { t[k] = strsep(&s, "\t"); if (!t[k]) break; }
		if (k < 5) continue;
		snprintf(e.folder, sizeof e.folder, "%s", t[0]);
		snprintf(e.file, sizeof e.file, "%s", t[1]);
		e.size = atoll(t[2]);
		e.mtime = atoll(t[3]);
		e.flag = atoi(t[4]);
		if (g_cn == g_ccap) {
			centry *n = realloc(g_c, (size_t)(g_ccap ? g_ccap * 2 : 256) * sizeof *n);
			if (!n) break;
			g_c = n;
			g_ccap = g_ccap ? g_ccap * 2 : 256;
		}
		g_c[g_cn++] = e;
	}
	fclose(f);
}

static int cache_find(const char *folder, const char *file, long long size, long long mtime)
{
	int i;

	for (i = 0; i < g_cn; i++)
		if (g_c[i].size == size && g_c[i].mtime == mtime && !strcmp(g_c[i].file, file) &&
		    !strcmp(g_c[i].folder, folder))
			return g_c[i].flag;
	return -2;
}

/* ---- scanning ---- */

static int scan_folder(const char *roms, const char *folder, FILE *cout, gbs_move *out, int n, int max)
{
	char dir[600];
	DIR *d;
	struct dirent *e;

	snprintf(dir, sizeof dir, "%s/%s", roms, folder);
	if (!(d = opendir(dir))) return n;
	while ((e = readdir(d))) {
		char p[900];
		struct stat st;
		int flag;
		const char *dot;

		if (!gb_file(e->d_name)) continue;
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		if (stat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		flag = cache_find(folder, e->d_name, (long long)st.st_size, (long long)st.st_mtime);
		if (flag == -2) flag = gc_cgb_flag(p);
		if (cout)
			fprintf(cout, "%s\t%s\t%lld\t%lld\t%d\n", folder, e->d_name,
			        (long long)st.st_size, (long long)st.st_mtime, flag);
		if (flag < 0 || !strcmp(gbs_home(flag), folder) || n >= max) continue;
		{
			gbs_move *m = &out[n++];
			char t[900];

			memset(m, 0, sizeof *m);
			snprintf(m->file, sizeof m->file, "%s", e->d_name);
			snprintf(m->name, sizeof m->name, "%s", e->d_name);
			if ((dot = strrchr(m->name, '.'))) m->name[dot - m->name] = '\0';
			m->from = !strcmp(folder, GBS_GB) ? GBS_GB : GBS_GBC;
			m->to = gbs_home(flag);
			snprintf(t, sizeof t, "%s/%s/%s", roms, m->to, m->file);
			m->clash = access(t, F_OK) == 0;
		}
	}
	closedir(d);
	return n;
}

int gbs_scan(const char *roms, const char *cache, gbs_move *out, int max)
{
	char tmp[600], a[600], b[600];
	FILE *cout = NULL;
	int n;

	snprintf(a, sizeof a, "%s/%s", roms, GBS_GB);
	snprintf(b, sizeof b, "%s/%s", roms, GBS_GBC);
	if (access(a, R_OK) != 0 && access(b, R_OK) != 0) return -1;
	cache_load(cache);
	if (cache) {
		snprintf(tmp, sizeof tmp, "%s.new", cache);
		cout = fopen(tmp, "w");
	}
	n = scan_folder(roms, GBS_GB, cout, out, 0, max);
	n = scan_folder(roms, GBS_GBC, cout, out, n, max);
	if (cout) {
		if (fclose(cout) == 0) rename(tmp, cache);
		else unlink(tmp);
	}
	free(g_c);
	g_c = NULL;
	g_cn = g_ccap = 0;
	return n;
}

/* ---- moving ---- */

/* rename, or copy and remove where rename can't */
static bool move_one(const char *from, const char *to)
{
	FILE *in, *out;
	char buf[65536];
	size_t got;
	bool ok = true;

	if (rename(from, to) == 0) return true;
	if (errno != EXDEV) return false;
	if (!(in = fopen(from, "rb"))) return false;
	if (!(out = fopen(to, "wb"))) { fclose(in); return false; }
	while ((got = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, got, out) != got) { ok = false; break; }
	fclose(in);
	if (fclose(out) != 0) ok = false;
	if (ok) unlink(from); else unlink(to);
	return ok;
}

static void mkdirs(const char *path)
{
	char p[900], *s;

	snprintf(p, sizeof p, "%s", path);
	for (s = p + 1; *s; s++)
		if (*s == '/') { *s = '\0'; mkdir(p, 0755); *s = '/'; }
	mkdir(p, 0755);
}

/* Every file in `dir` named `stem`, a dot and then the rest, to the same name
 * in `todir`. `one_ext`: the rest is a single extension, so "Tetris" takes
 * Tetris.png and not Tetris.DX.png, another game's. */
static void move_prefixed(const char *dir, const char *todir, const char *stem, bool one_ext)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	size_t k = strlen(stem);

	if (!d) return;
	while ((e = readdir(d))) {
		char f[900], t[900];

		if (strncmp(e->d_name, stem, k) || e->d_name[k] != '.') continue;
		if (one_ext && strchr(e->d_name + k + 1, '.')) continue;
		mkdirs(todir);
		snprintf(f, sizeof f, "%s/%s", dir, e->d_name);
		snprintf(t, sizeof t, "%s/%s", todir, e->d_name);
		if (access(t, F_OK) != 0) move_one(f, t);
	}
	closedir(d);
}

bool gbs_move_files(const char *roms, const char *shared, const char *card, const gbs_move *m)
{
	char f[900], t[900];

	if (m->clash) return false;
	snprintf(f, sizeof f, "%s/%s/%s", roms, m->from, m->file);
	snprintf(t, sizeof t, "%s/%s", roms, m->to);
	mkdirs(t);
	snprintf(t, sizeof t, "%s/%s/%s", roms, m->to, m->file);
	if (access(t, F_OK) == 0 || !move_one(f, t)) return false;
	/* box art: <name>.png and any other picture of that name */
	snprintf(f, sizeof f, "%s/%s/.media", roms, m->from);
	snprintf(t, sizeof t, "%s/%s/.media", roms, m->to);
	move_prefixed(f, t, m->name, true);
	/* states and their pictures: <file>.<slot>.state / .bmp */
	snprintf(f, sizeof f, "%s/.tortos/%s", shared, m->from);
	snprintf(t, sizeof t, "%s/.tortos/%s", shared, m->to);
	move_prefixed(f, t, m->file, false);
	/* cheats */
	snprintf(f, sizeof f, "%s/Cheats/%s/%s.cht", card, m->from, m->name);
	if (access(f, F_OK) == 0) {
		snprintf(t, sizeof t, "%s/Cheats/%s", card, m->to);
		mkdirs(t);
		snprintf(t, sizeof t, "%s/Cheats/%s/%s.cht", card, m->to, m->name);
		if (access(t, F_OK) != 0) move_one(f, t);
	}
	return true;
}
