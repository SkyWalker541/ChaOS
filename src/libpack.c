/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: a port's libraries, and ChaOS's pack of them - see libpack.h */
#include "libpack.h"
#include "rajson.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- one ELF --------------------------------------------------------------- */

static bool rd(int fd, uint64_t off, void *buf, size_t n)
{
	return pread(fd, buf, n, (off_t)off) == (ssize_t)n;
}

static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const unsigned char *p) { return u32(p) | (uint64_t)u32(p + 4) << 32; }

bool libpack_elf(const char *path, char needed[][LIBPACK_NAME], int max, int *n,
                 char *soname, size_t sn)
{
	unsigned char h[64], ph[56], d[16];
	uint64_t phoff, dyn_off = 0, dyn_sz = 0, strtab_va = 0, strtab = 0;
	uint64_t load_va[16], load_off[16], load_sz[16];
	uint64_t want[64], so = UINT64_MAX;
	int nload = 0, nwant = 0, i, fd;
	uint16_t phnum, phent;

	*n = 0;
	if (soname && sn) soname[0] = '\0';
	fd = open(path, O_RDONLY);
	if (fd < 0) return false;
	if (!rd(fd, 0, h, 64) || memcmp(h, "\x7f" "ELF", 4) || h[4] != 2 || h[5] != 1) {
		close(fd);
		return false;
	}
	phoff = u64(h + 32);
	phent = u16(h + 54);
	phnum = u16(h + 56);
	for (i = 0; i < phnum && i < 64; i++) {
		uint32_t type;

		if (phent < 56 || !rd(fd, phoff + (uint64_t)i * phent, ph, 56)) break;
		type = u32(ph);
		if (type == 1 && nload < 16) {          /* PT_LOAD */
			load_off[nload] = u64(ph + 8);
			load_va[nload] = u64(ph + 16);
			load_sz[nload] = u64(ph + 32);
			nload++;
		} else if (type == 2) {                 /* PT_DYNAMIC */
			dyn_off = u64(ph + 8);
			dyn_sz = u64(ph + 32);
		}
	}
	if (!dyn_sz) { close(fd); return true; }    /* static: needs nothing */
	for (i = 0; (uint64_t)(i + 1) * 16 <= dyn_sz && i < 512; i++) {
		int64_t tag;
		uint64_t val;

		if (!rd(fd, dyn_off + (uint64_t)i * 16, d, 16)) break;
		tag = (int64_t)u64(d);
		val = u64(d + 8);
		if (tag == 0) break;
		if (tag == 1 && nwant < 64) want[nwant++] = val;   /* DT_NEEDED */
		else if (tag == 5) strtab_va = val;                /* DT_STRTAB */
		else if (tag == 14) so = val;                      /* DT_SONAME */
	}
	for (i = 0; i < nload; i++)
		if (strtab_va >= load_va[i] && strtab_va < load_va[i] + load_sz[i])
			strtab = strtab_va - load_va[i] + load_off[i];
	if (!strtab) { close(fd); return true; }
	for (i = 0; i < nwant; i++) {
		char s[LIBPACK_NAME];
		ssize_t got = pread(fd, s, sizeof s - 1, (off_t)(strtab + want[i]));

		if (got <= 0) continue;
		s[got] = '\0';
		if (!s[0] || strlen(s) >= sizeof s - 1) continue;
		if (*n < max) snprintf(needed[*n], LIBPACK_NAME, "%s", s);
		(*n)++;
	}
	if (*n > max) *n = max;
	if (so != UINT64_MAX && soname && sn > 1) {
		ssize_t got = pread(fd, soname, sn - 1, (off_t)(strtab + so));

		soname[got > 0 ? (size_t)got : 0] = '\0';   /* up to its own NUL */
	}
	close(fd);
	return true;
}

/* ---- a port's folder ------------------------------------------------------- */

typedef struct {
	char base[LIBPACK_NAME];
	char soname[LIBPACK_NAME];
	char (*needed)[LIBPACK_NAME];
	int nneeded;
	bool lib;
} elf_ent;

typedef struct {
	char (*names)[LIBPACK_NAME];   /* every file the port carries, by name */
	int nnames, capnames;
	elf_ent *e;
	int ne, cape;
	int files;
} walk;

/* The file kinds that are never programs: not even opened */
static bool data_name(const char *n)
{
	static const char *const ext[] = {
		"png", "jpg", "jpeg", "ogg", "wav", "mp3", "txt", "md", "json", "xml", "gptk", "pck",
		"wad", "pak", "dat", "cfg", "ini", "lua", "py", "sh", "ttf", "otf", "zip", "love",
		"apk", "win", "ogv", "webm", "mp4", "bmp", "gif", "tga", "csv", "html", "css", "js",
		"yml", "yaml", "squashfs", "droid", "port", "svg", "mod", "xm", "it", "s3m", "mid",
		"sf2", "sf3", "flac", "opus", "m4a", "glsl", "vert", "frag", "shader", "po", "mo",
		"pdf", "gz", "xz", "7z", "rar", "tar", "rom", "iso", "cue", "bik", "dll", "exe",
		"pdb", "jar", "class", "rpy", "rpyc", "rpa", "pyc", "pyo", "data", "unity3d",
		"assets", "ress", "log", "save", "sav", "bak", "srm", "state", NULL
	};
	const char *dot = strrchr(n, '.');
	int i;

	if (strstr(n, ".so")) return false;
	if (!dot) return false;
	for (i = 0; ext[i]; i++)
		if (!strcasecmp(dot + 1, ext[i])) return true;
	return false;
}

static void add_name(walk *w, const char *n)
{
	if (w->nnames == w->capnames) {
		int cap = w->capnames ? w->capnames * 2 : 256;
		char (*nn)[LIBPACK_NAME] = realloc(w->names, (size_t)cap * LIBPACK_NAME);

		if (!nn) return;
		w->names = nn;
		w->capnames = cap;
	}
	snprintf(w->names[w->nnames++], LIBPACK_NAME, "%s", n);
}

static void scan_dir(walk *w, const char *dir, int depth)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d) return;
	while ((e = readdir(d)) && w->files < 40000) {
		char path[1024];
		struct stat st;

		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (stat(path, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth < 8) scan_dir(w, path, depth + 1);
			continue;
		}
		if (!S_ISREG(st.st_mode)) continue;
		w->files++;
		add_name(w, e->d_name);
		if (data_name(e->d_name) || st.st_size < 64) continue;
		{
			char needed[64][LIBPACK_NAME], so[LIBPACK_NAME];
			int nn = 0;
			elf_ent *x;

			if (!libpack_elf(path, needed, 64, &nn, so, sizeof so)) continue;
			if (w->ne == w->cape) {
				int cap = w->cape ? w->cape * 2 : 32;
				elf_ent *ne = realloc(w->e, (size_t)cap * sizeof *ne);

				if (!ne) continue;
				w->e = ne;
				w->cape = cap;
			}
			x = &w->e[w->ne];
			memset(x, 0, sizeof *x);
			snprintf(x->base, sizeof x->base, "%s", e->d_name);
			snprintf(x->soname, sizeof x->soname, "%s", so);
			x->lib = so[0] || strstr(e->d_name, ".so") != NULL;
			if (nn) {
				x->needed = malloc((size_t)nn * LIBPACK_NAME);
				if (x->needed) {
					memcpy(x->needed, needed, (size_t)nn * LIBPACK_NAME);
					x->nneeded = nn;
				}
			}
			w->ne++;
		}
	}
	closedir(d);
}

static const elf_ent *carried_lib(const walk *w, const char *n)
{
	int i;

	for (i = 0; i < w->ne; i++)
		if (w->e[i].lib && (!strcmp(w->e[i].base, n) || !strcmp(w->e[i].soname, n)))
			return &w->e[i];
	return NULL;
}

static bool carried(const walk *w, const char *n)
{
	int i;

	for (i = 0; i < w->nnames; i++)
		if (!strcmp(w->names[i], n)) return true;
	return false;
}

static bool on_device(const char *const *dirs, const char *n)
{
	char p[512];

	for (; dirs && *dirs; dirs++) {
		snprintf(p, sizeof p, "%s/%s", *dirs, n);
		if (access(p, F_OK) == 0) return true;
	}
	return false;
}

int libpack_missing(const char *port_dir, const char *const *have_dirs,
                    char out[][LIBPACK_NAME], int max)
{
	walk w;
	char (*seen)[LIBPACK_NAME] = NULL, (*todo)[LIBPACK_NAME] = NULL;
	int nseen = 0, nmiss = 0, i;
	const int cap = 2048;

	memset(&w, 0, sizeof w);
	scan_dir(&w, port_dir, 0);
	seen = malloc((size_t)cap * LIBPACK_NAME);
	todo = malloc((size_t)cap * LIBPACK_NAME);
	if (!seen || !todo) goto done;
	for (i = 0; i < w.ne; i++) {
		int nt = 0, k;

		if (w.e[i].lib) continue;              /* reached from a program */
		for (k = 0; k < w.e[i].nneeded && nt < cap; k++)
			snprintf(todo[nt++], LIBPACK_NAME, "%s", w.e[i].needed[k]);
		while (nt > 0) {
			char n[LIBPACK_NAME];
			const elf_ent *lib;
			bool was = false;

			snprintf(n, sizeof n, "%s", todo[--nt]);
			for (k = 0; k < nseen && !was; k++) was = !strcmp(seen[k], n);
			if (was || nseen >= cap) continue;
			snprintf(seen[nseen++], LIBPACK_NAME, "%s", n);
			if ((lib = carried_lib(&w, n))) {
				for (k = 0; k < lib->nneeded && nt < cap; k++)
					snprintf(todo[nt++], LIBPACK_NAME, "%s", lib->needed[k]);
				continue;
			}
			if (carried(&w, n) || on_device(have_dirs, n)) continue;
			for (k = 0; k < nmiss && k < max && strcmp(out[k], n); k++) ;
			if (k < nmiss && k < max) continue;
			if (nmiss < max) snprintf(out[nmiss], LIBPACK_NAME, "%s", n);
			nmiss++;
		}
	}
done:
	for (i = 0; i < w.ne; i++) free(w.e[i].needed);
	free(w.e);
	free(w.names);
	free(seen);
	free(todo);
	return nmiss < max ? nmiss : max;
}

/* ---- the pack's list ------------------------------------------------------- */

bool libpack_find(const char *list_json, size_t len, const char *name,
                  char *file, size_t fn, char *md5, size_t mn, long *size)
{
	jsv root = js_root(list_json, len), libs, e, x;

	file[0] = md5[0] = '\0';
	*size = 0;
	if (!js_member(root, "libs", &libs) || !js_member(libs, name, &e)) return false;
	if (js_member(e, "md5", &x)) js_str(x, md5, mn);
	if (js_member(e, "size", &x)) *size = js_int(x);
	return js_member(e, "file", &x) && js_str(x, file, fn) && file[0] && !strchr(file, '/');
}
