/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: games on more than one disc, made playlists. See discset.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "discset.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* The images a playlist lists, best first when one disc comes in two */
static const char *const IMAGES[] = { "chd", "cue", "ccd", "pbp", "iso", "img", "toc", NULL };

static const char *ext_of(const char *n)
{
	const char *d = strrchr(n, '.');

	return d && d != n ? d + 1 : "";
}

static int image_rank(const char *name)
{
	int i;

	for (i = 0; IMAGES[i]; i++) if (!strcasecmp(ext_of(name), IMAGES[i])) return i;
	return -1;
}

/* ---- the disc a name says ------------------------------------------------- */

static void trim(char *s)
{
	char *r = s, *w = s;
	size_t l;

	/* spaces collapsed, empty brackets dropped */
	while (*r) {
		if ((r[0] == '(' && r[1] == ')') || (r[0] == '[' && r[1] == ']')) { r += 2; continue; }
		if (*r == ' ' && (w == s || w[-1] == ' ')) { r++; continue; }
		*w++ = *r++;
	}
	*w = '\0';
	l = strlen(s);
	while (l && strchr(" -_.,", s[l - 1])) s[--l] = '\0';
	while (*s && strchr(" -_.,", *s)) memmove(s, s + 1, strlen(s));
}

int ds_disc_of(const char *name, char *base, size_t bn)
{
	char n[512];
	const char *dot = strrchr(name, '.');
	size_t l = dot && dot != name ? (size_t)(dot - name) : strlen(name);
	char *p;

	if (base && bn) base[0] = '\0';
	if (l >= sizeof n) return 0;
	memcpy(n, name, l);
	n[l] = '\0';
	for (p = n; *p; p++) {
		static const char *const words[] = { "disc", "disk", "cd", NULL };
		char *q, *s, *e;
		int w, num = 0;

		if (p > n && isalnum((unsigned char)p[-1])) continue;
		for (w = 0; words[w]; w++) if (!strncasecmp(p, words[w], strlen(words[w]))) break;
		if (!words[w]) continue;
		q = p + strlen(words[w]);
		while (*q == ' ' || *q == '_' || *q == '-' || *q == '#' || *q == '.') q++;
		if (isdigit((unsigned char)*q)) {
			num = (int)strtol(q, &e, 10);
			q = e;
		} else if (*q && strchr("ABCDEFabcdef", *q) && !isalnum((unsigned char)q[1])) {
			num = toupper((unsigned char)*q) - 'A' + 1;
			q++;
		} else {
			continue;
		}
		/* "Disc2of2" */
		if (!strncasecmp(q, "of", 2) && isdigit((unsigned char)q[2])) { strtol(q + 2, &e, 10); q = e; }
		if (isalnum((unsigned char)*q) || num <= 0 || num > 20) continue;
		/* " of 3" */
		{
			char *o = q;

			while (*o == ' ') o++;
			if (!strncasecmp(o, "of", 2) && !isalnum((unsigned char)o[2])) {
				o += 2;
				while (*o == ' ') o++;
				if (isdigit((unsigned char)*o)) { strtol(o, &e, 10); q = e; }
			}
		}
		/* the span: its brackets, when it is alone in them */
		s = p;
		e = q;
		if (s > n && (s[-1] == '(' || s[-1] == '[') && (*e == ')' || *e == ']')) { s--; e++; }
		if (base && bn) {
			char out[512];

			snprintf(out, sizeof out, "%.*s %s", (int)(s - n), n, e);
			trim(out);
			snprintf(base, bn, "%s", out);
		}
		return num;
	}
	return 0;
}

/* ---- finding sets --------------------------------------------------------- */

typedef struct { char file[256]; char base[200]; int num; } disc;

static bool is_dir_at(const char *dir, const char *name)
{
	char p[1024];
	struct stat st;

	snprintf(p, sizeof p, "%s/%s", dir, name);
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool exists_at(const char *dir, const char *name)
{
	char p[1024];

	snprintf(p, sizeof p, "%s/%s", dir, name);
	return access(p, F_OK) == 0;
}

static int disc_cmp(const void *x, const void *y)
{
	const disc *a = x, *b = y;
	int c = strcasecmp(a->base, b->base);

	if (c) return c;
	if (a->num != b->num) return a->num - b->num;
	return image_rank(a->file) - image_rank(b->file);
}

/* The files a .cue or .ccd brings with it: its tracks */
static void companions(const char *dir, const char *file, ds_set *s)
{
	char p[1024], line[600];
	FILE *f;

	if (!strcasecmp(ext_of(file), "ccd")) {
		static const char *const more[] = { "img", "sub", NULL };
		char stem[256], *d;
		int i;

		snprintf(stem, sizeof stem, "%s", file);
		if ((d = strrchr(stem, '.'))) *d = '\0';
		for (i = 0; more[i] && s->nf < DS_FILES; i++) {
			char c[300];

			snprintf(c, sizeof c, "%s.%s", stem, more[i]);
			if (exists_at(dir, c)) snprintf(s->files[s->nf++], sizeof s->files[0], "%s", c);
		}
		return;
	}
	if (strcasecmp(ext_of(file), "cue")) return;
	snprintf(p, sizeof p, "%s/%s", dir, file);
	if (!(f = fopen(p, "r"))) return;
	while (fgets(line, sizeof line, f) && s->nf < DS_FILES) {
		char *a = line, *q1, *q2;
		int i;
		bool dup = false;

		while (*a == ' ' || *a == '\t') a++;
		if (strncasecmp(a, "FILE", 4)) continue;
		if (!(q1 = strchr(a, '"')) || !(q2 = strchr(q1 + 1, '"'))) continue;
		*q2 = '\0';
		q1++;
		if (strchr(q1, '/') || strchr(q1, '\\') || !exists_at(dir, q1)) continue;
		for (i = 0; i < s->nf; i++) if (!strcmp(s->files[i], q1)) dup = true;
		if (!dup) snprintf(s->files[s->nf++], sizeof s->files[0], "%s", q1);
	}
	fclose(f);
}

/* Discs into sets: runs of the same game with two or more numbers, no two
 * alike (one image a number, the best kind when there are two) */
static int group(disc *d, int nd, ds_set *out, int max, const char *folder)
{
	int i = 0, n = 0;

	qsort(d, (size_t)nd, sizeof *d, disc_cmp);
	while (i < nd && n < max) {
		int j = i, k, distinct = 0, last = 0;

		while (j < nd && !strcasecmp(d[j].base, d[i].base)) j++;
		for (k = i; k < j; k++) if (d[k].num != last) { distinct++; last = d[k].num; }
		if (distinct >= 2 && distinct <= DS_DISCS && d[i].base[0]) {
			ds_set *s = &out[n];

			memset(s, 0, sizeof *s);
			snprintf(s->folder, sizeof s->folder, "%s", folder);
			snprintf(s->base, sizeof s->base, "%s", d[i].base);
			last = 0;
			for (k = i; k < j; k++) {
				if (d[k].num == last) continue;        /* the same disc in a worse kind */
				last = d[k].num;
				snprintf(s->discs[s->n++], sizeof s->discs[0], "%s", d[k].file);
			}
			n++;
		}
		i = j;
	}
	return n;
}

/* What library.c's folder_launch_file picks, so records follow the game */
static void launched(const char *dir, char *out, size_t n)
{
	static const char *const RANK[] = { "m3u", "chd", "cue", "ccd", "toc", "iso", "img", "pbp", "bin", NULL };
	DIR *d = opendir(dir);
	struct dirent *e;
	int best = 999;

	out[0] = '\0';
	if (!d) return;
	while ((e = readdir(d))) {
		int r;

		if (e->d_name[0] == '.') continue;
		for (r = 0; RANK[r]; r++) if (!strcasecmp(ext_of(e->d_name), RANK[r])) break;
		if (!RANK[r]) continue;
		if (r < best || (r == best && strcasecmp(e->d_name, out) < 0)) {
			best = r;
			snprintf(out, n, "%s", e->d_name);
		}
	}
	closedir(d);
}

int ds_scan(const char *roms, const char *folder, ds_set *out, int max)
{
	char dir[1024];
	DIR *d;
	struct dirent *e;
	disc *loose = NULL;
	int nl = 0, cap = 0, n = 0, i, k;

	snprintf(dir, sizeof dir, "%s/%s", roms, folder);
	if (!(d = opendir(dir))) return -1;
	while ((e = readdir(d))) {
		disc one;

		if (e->d_name[0] == '.') continue;
		if (is_dir_at(dir, e->d_name)) {
			/* a game's folder with its discs and no playlist */
			char sub[1024], has_m3u = 0;
			DIR *sd;
			struct dirent *se;
			disc in[DS_FILES];
			int ni = 0, got;
			ds_set s1;

			if (n >= max) continue;
			snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
			if (!(sd = opendir(sub))) continue;
			while ((se = readdir(sd))) {
				if (se->d_name[0] == '.') continue;
				if (!strcasecmp(ext_of(se->d_name), "m3u")) has_m3u = 1;
				if (image_rank(se->d_name) < 0 || ni >= DS_FILES) continue;
				memset(&in[ni], 0, sizeof in[ni]);
				snprintf(in[ni].file, sizeof in[ni].file, "%s", se->d_name);
				if ((in[ni].num = ds_disc_of(se->d_name, in[ni].base, sizeof in[ni].base)) > 0) ni++;
			}
			closedir(sd);
			if (has_m3u || ni < 2) continue;
			got = group(in, ni, &s1, 1, folder);
			if (got != 1) continue;
			/* the folder is the game: the playlist is named for it */
			snprintf(s1.base, sizeof s1.base, "%s", e->d_name);
			snprintf(s1.dir, sizeof s1.dir, "%s", e->d_name);
			s1.loose = false;
			{
				char l[256];

				launched(sub, l, sizeof l);
				snprintf(s1.was, sizeof s1.was, "%s/%s", e->d_name, l);
			}
			snprintf(s1.now, sizeof s1.now, "%s/%s.m3u", e->d_name, e->d_name);
			if (exists_at(sub, strrchr(s1.now, '/') + 1)) s1.clash = true;
			out[n++] = s1;
			continue;
		}
		if (image_rank(e->d_name) < 0) continue;
		memset(&one, 0, sizeof one);
		snprintf(one.file, sizeof one.file, "%s", e->d_name);
		if ((one.num = ds_disc_of(e->d_name, one.base, sizeof one.base)) <= 0) continue;
		if (nl == cap) {
			disc *nd = realloc(loose, (size_t)(cap = cap ? cap * 2 : 32) * sizeof *nd);

			if (!nd) break;
			loose = nd;
		}
		loose[nl++] = one;
	}
	closedir(d);
	if (nl && n < max) {
		int got = group(loose, nl, out + n, max - n, folder);

		for (i = n; i < n + got; i++) {
			ds_set *s = &out[i];

			s->loose = true;
			snprintf(s->dir, sizeof s->dir, "%s", s->base);
			for (k = 0; k < s->n && s->nf < DS_FILES; k++) {
				snprintf(s->files[s->nf++], sizeof s->files[0], "%s", s->discs[k]);
				companions(dir, s->discs[k], s);
			}
			snprintf(s->was, sizeof s->was, "%s", s->discs[0]);
			snprintf(s->now, sizeof s->now, "%s/%s.m3u", s->dir, s->base);
			/* the game's folder can't be made where something has the name */
			if (exists_at(dir, s->dir)) s->clash = true;
		}
		n += got;
	}
	free(loose);
	return n;
}

/* ---- making one ------------------------------------------------------------ */

static bool move_one(const char *from, const char *to)
{
	char buf[65536];
	FILE *in, *out;
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

static bool copy_one(const char *from, const char *to)
{
	char buf[65536];
	FILE *in, *out;
	size_t got;
	bool ok = true;

	if (!(in = fopen(from, "rb"))) return false;
	if (!(out = fopen(to, "wb"))) { fclose(in); return false; }
	while ((got = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, got, out) != got) { ok = false; break; }
	fclose(in);
	if (fclose(out) != 0) ok = false;
	if (!ok) unlink(to);
	return ok;
}

static void mkdirs(const char *path)
{
	char p[1024], *s;

	snprintf(p, sizeof p, "%s", path);
	for (s = p + 1; *s; s++)
		if (*s == '/') { *s = '\0'; mkdir(p, 0755); *s = '/'; }
	mkdir(p, 0755);
}

/* "a/b/c.chd" into "a/b" and "c.chd" */
static void split_path(const char *rel, char *dir, size_t dn, const char **name)
{
	const char *sl = strrchr(rel, '/');

	if (sl) { snprintf(dir, dn, "%.*s", (int)(sl - rel), rel); *name = sl + 1; }
	else { dir[0] = '\0'; *name = rel; }
}

/* Every file in `fdir` named `from` and then a dot, moved to `tdir` as `to`
 * and the same rest: one game's states, under the name it plays as now */
static void move_renamed(const char *fdir, const char *from, const char *tdir, const char *to)
{
	DIR *d = opendir(fdir);
	struct dirent *e;
	size_t k = strlen(from);

	if (!d) return;
	while ((e = readdir(d))) {
		char f[1024], t[1024];

		if (strncmp(e->d_name, from, k) || e->d_name[k] != '.') continue;
		mkdirs(tdir);
		snprintf(f, sizeof f, "%s/%s", fdir, e->d_name);
		snprintf(t, sizeof t, "%s/%s%s", tdir, to, e->d_name + k);
		if (access(t, F_OK) != 0) move_one(f, t);
	}
	closedir(d);
}

static void stem_of(const char *file, char *out, size_t n)
{
	const char *sl = strrchr(file, '/'), *b = sl ? sl + 1 : file, *dot = strrchr(b, '.');

	snprintf(out, n, "%.*s", dot ? (int)(dot - b) : (int)strlen(b), b);
}

bool ds_apply(const char *roms, const char *shared, const char *card, const ds_set *s)
{
	char cdir[1024], gdir[1024], f[1200], t[1200];
	int i, moved = 0;

	if (s->clash || s->n < 2) return false;
	snprintf(cdir, sizeof cdir, "%s/%s", roms, s->folder);
	snprintf(gdir, sizeof gdir, "%s/%s", cdir, s->dir);
	if (s->loose) {
		if (mkdir(gdir, 0755) != 0 && errno != EEXIST) return false;
		for (i = 0; i < s->nf; i++) {
			snprintf(f, sizeof f, "%s/%s", cdir, s->files[i]);
			snprintf(t, sizeof t, "%s/%s", gdir, s->files[i]);
			if (access(t, F_OK) == 0 || !move_one(f, t)) break;
			moved++;
		}
		if (moved < s->nf) {
			/* put back what went, and give up */
			for (i = 0; i < moved; i++) {
				snprintf(f, sizeof f, "%s/%s", gdir, s->files[i]);
				snprintf(t, sizeof t, "%s/%s", cdir, s->files[i]);
				move_one(f, t);
			}
			rmdir(gdir);
			return false;
		}
	}
	/* the playlist */
	{
		char m3u[1200], tmp[1210];
		FILE *o;

		snprintf(m3u, sizeof m3u, "%s/%s.m3u", gdir, s->base);
		snprintf(tmp, sizeof tmp, "%s.part", m3u);
		if (!(o = fopen(tmp, "w"))) return false;
		for (i = 0; i < s->n; i++) fprintf(o, "%s\n", s->discs[i]);
		if (fclose(o) != 0 || rename(tmp, m3u) != 0) { unlink(tmp); return false; }
	}
	/* the save, under the name the playlist plays as; the first disc's kept */
	{
		char was[256], now[256];

		stem_of(s->was, was, sizeof was);
		stem_of(s->now, now, sizeof now);
		snprintf(f, sizeof f, "%s/Saves/%s.srm", card, was);
		snprintf(t, sizeof t, "%s/Saves/%s.srm", card, now);
		if (strcmp(was, now) && access(f, F_OK) == 0 && access(t, F_OK) != 0) copy_one(f, t);
	}
	/* the first disc's states, under the playlist's name */
	{
		char wd[600], nd[600], fd[1200], td[1200];
		const char *wn, *nn;

		split_path(s->was, wd, sizeof wd, &wn);
		split_path(s->now, nd, sizeof nd, &nn);
		/* ChaOS 2.7.3: states live in Saves/States/<console>/ */
		(void)shared;
		snprintf(fd, sizeof fd, "%s/Saves/States/%s%s%s", card, s->folder, wd[0] ? "/" : "", wd);
		snprintf(td, sizeof td, "%s/Saves/States/%s%s%s", card, s->folder, nd[0] ? "/" : "", nd);
		move_renamed(fd, wn, td, nn);
	}
	/* box art: a loose first disc's picture is the game's now */
	if (s->loose) {
		char was[256], media[1100];
		static const char *const pics[] = { "png", "jpg", "jpeg", "webp", NULL };

		stem_of(s->was, was, sizeof was);
		snprintf(media, sizeof media, "%s/.media", cdir);
		for (i = 0; pics[i]; i++) {
			snprintf(f, sizeof f, "%s/%s.%s", media, was, pics[i]);
			snprintf(t, sizeof t, "%s/%s.%s", media, s->base, pics[i]);
			if (access(f, F_OK) == 0 && access(t, F_OK) != 0) { copy_one(f, t); break; }
		}
	}
	sync();
	return true;
}
