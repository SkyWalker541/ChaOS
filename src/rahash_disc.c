/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: RetroAchievements' hash of a PlayStation disc. See rahash.h.
 *
 * Their rule (rcheevos, src/hash/rc_hash_disc.c, rc_hash_psx): find SYSTEM.CNF
 * on the disc's first track, read its BOOT line - "BOOT = cdrom:\SLUS_007.00;1"
 * - for the program the console starts, and hash that program's name
 * ("SLUS_007.00", without the ;1) followed by the program itself, its size
 * from its own PS-X EXE header plus the 2048-byte header. No SYSTEM.CNF:
 * PSX.EXE, by the same rule. The ISO 9660 walk, the BOOT line's parsing and the
 * sector arithmetic below follow theirs line for line (rcheevos is MIT), so
 * that a disc hashes here as it does in RetroArch.
 *
 * The disc as a file: a .cue's first FILE (its first track), a .bin, .iso or
 * .img alone, a .ccd's .img, an .m3u's first disc, or a .chd, read with
 * libchdr (lib/libchdr). A sector is told raw (2352 bytes, its data 24 bytes
 * in for Mode 2, 16 for Mode 1), Mode 2 without its sync (2336, 8 in), or
 * cooked (2048) by looking, as rcheevos does, for the sync pattern and for
 * "CD001" at sector 16. Never the whole image read in: a disc is up to 700MB,
 * on a device with 1GB. .pbp - a PSP's EBOOT, compressed - is not read. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rahash.h"
#include "../lib/libchdr/include/libchdr/chd.h"

/* MD5, from rahash.c */
void ra_md5_begin(void **ctx);
void ra_md5_add(void *ctx, const void *data, size_t len);
void ra_md5_end(void *ctx, char *out);

#define MAX_EXE (64u * 1024 * 1024)   /* rcheevos' MAX_BUFFER_SIZE */

typedef struct {
	FILE    *f;
	long     base;          /* where the track's first sector starts in it */
	chd_file *chd;
	uint8_t *hunk;          /* the chd's last hunk read, and which */
	uint32_t hunk_at, frames_per_hunk, frame_bytes, first_frame;
	int      sector_size;   /* 2352, 2336 or 2048 */
	int      header;        /* bytes before a sector's data */
} disc;

/* One sector's bytes as stored, into `raw` (2448 at most) */
static bool raw_sector(disc *d, uint32_t lba, uint8_t *raw, int want)
{
	if (d->chd) {
		uint32_t frame = d->first_frame + lba;
		uint32_t h = frame / d->frames_per_hunk;

		if (h != d->hunk_at) {
			if (chd_read(d->chd, h, d->hunk) != CHDERR_NONE) return false;
			d->hunk_at = h;
		}
		memcpy(raw, d->hunk + (size_t)(frame % d->frames_per_hunk) * d->frame_bytes, (size_t)want);
		return true;
	}
	if (fseek(d->f, d->base + (long)lba * d->sector_size, SEEK_SET) != 0) return false;
	return fread(raw, 1, (size_t)want, d->f) == (size_t)want;
}

/* The data of sector `lba`, `n` bytes of it (2048 at most) */
static size_t read_sector(disc *d, uint32_t lba, uint8_t *out, size_t n)
{
	uint8_t raw[2448];

	if (n > 2048) n = 2048;
	if (!raw_sector(d, lba, raw, d->header + (int)n)) return 0;
	memcpy(out, raw + d->header, n);
	return n;
}

/* Raw, raw without sync, or cooked: rcheevos' cdreader_determine_sector_size */
static void sector_kind(disc *d)
{
	static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
	uint8_t h[32];
	static const int sizes[] = { 2352, 2336 };
	int i;

	for (i = 0; i < 2; i++) {
		d->sector_size = sizes[i];
		d->header = 0;
		if (raw_sector(d, 16, h, 32) && !memcmp(h, sync, 12)) {
			d->header = !memcmp(h + 25, "CD001", 5) ? 24 : 16;
			return;
		}
		if (d->chd) break;      /* a chd's frames are 2352, one way or the other */
	}
	d->sector_size = 2048;
	d->header = 0;
	if (d->chd) {
		/* a cooked chd track: its 2048 bytes at the frame's start */
		d->sector_size = 2352;
		return;
	}
	if (raw_sector(d, 16, h, 32) && !memcmp(h + 1, "CD001", 5)) return;
	/* By the file's size, as theirs falls back to */
	if (fseek(d->f, 0, SEEK_END) == 0) {
		long sz = ftell(d->f) - d->base;
		if (sz % 2352 == 0) { d->sector_size = 2352; d->header = 24; }
		else if (sz % 2336 == 0) { d->sector_size = 2336; d->header = 8; }
	}
}

static bool ext_is(const char *path, const char *ext)
{
	const char *dot = strrchr(path, '.');
	return dot && !strcasecmp(dot, ext);
}

/* `name` beside `path`, into `out` */
static void beside(const char *path, const char *name, char *out, size_t n)
{
	const char *slash = strrchr(path, '/');
	int dir = slash ? (int)(slash - path + 1) : 0;

	if (name[0] == '/') snprintf(out, n, "%s", name);
	else snprintf(out, n, "%.*s%s", dir, path, name);
}

/* A line's text, trimmed */
static char *trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t') s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
	return s;
}

/* The disc file a .cue, .ccd or .m3u leads to, with the first track's offset in
 * it (a cue's INDEX 01), into `out`; false when there is none */
static bool resolve(const char *path, char *out, size_t n, long *base, int depth)
{
	FILE *f;
	char line[1100], file[1024] = "";
	bool in_first = false;

	*base = 0;
	if (depth > 2) return false;
	if (ext_is(path, ".m3u")) {
		if (!(f = fopen(path, "r"))) return false;
		while (fgets(line, sizeof line, f)) {
			char *t = trim(line);
			if (!*t || *t == '#') continue;
			beside(path, t, file, sizeof file);
			break;
		}
		fclose(f);
		return file[0] && resolve(file, out, n, base, depth + 1);
	}
	if (ext_is(path, ".ccd")) {
		size_t l = strlen(path);
		snprintf(out, n, "%.*s.img", (int)(l - 4), path);
		return true;
	}
	if (!ext_is(path, ".cue")) {
		snprintf(out, n, "%s", path);
		return true;
	}
	if (!(f = fopen(path, "r"))) return false;
	while (fgets(line, sizeof line, f)) {
		char *t = trim(line);

		if (!strncasecmp(t, "FILE ", 5)) {
			char *q1, *q2;
			if (file[0]) break;            /* track 1 is in the first FILE */
			t += 5;
			if ((q1 = strchr(t, '"')) && (q2 = strchr(q1 + 1, '"'))) {
				*q2 = '\0';
				beside(path, q1 + 1, file, sizeof file);
			} else {
				char *sp = strrchr(t, ' ');
				if (sp) *sp = '\0';
				beside(path, t, file, sizeof file);
			}
		} else if (!strncasecmp(t, "TRACK ", 6)) {
			in_first = atoi(t + 6) == 1;
		} else if (in_first && !strncasecmp(t, "INDEX 01", 8)) {
			int m = 0, s = 0, fr = 0;
			sscanf(t + 8, " %d:%d:%d", &m, &s, &fr);
			/* in sectors: their size isn't known yet */
			*base = ((m * 60) + s) * 75 + fr;
		}
	}
	fclose(f);
	if (!file[0]) return false;
	snprintf(out, n, "%s", file);
	return true;
}

static bool disc_open(disc *d, const char *path)
{
	char file[1024];
	long idx = 0;

	memset(d, 0, sizeof *d);
	d->hunk_at = UINT32_MAX;
	if (ext_is(path, ".pbp")) return false;
	if (!resolve(path, file, sizeof file, &idx, 0)) return false;

	if (ext_is(file, ".chd")) {
		const chd_header *h;
		char meta[256];
		uint32_t len;
		int tr = 0, frames = 0, pregap = 0, postgap = 0;
		char type[32] = "", sub[32] = "", pgtype[32] = "";

		if (chd_open(file, CHD_OPEN_READ, NULL, &d->chd) != CHDERR_NONE) return false;
		h = chd_get_header(d->chd);
		d->frame_bytes = h->unitbytes ? h->unitbytes : 2448;
		d->frames_per_hunk = h->hunkbytes / d->frame_bytes;
		if (!d->frames_per_hunk || !(d->hunk = malloc(h->hunkbytes))) {
			chd_close(d->chd);
			return false;
		}
		/* Track 1, and whether its pregap is stored (PGTYPE V...) */
		if (chd_get_metadata(d->chd, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof meta,
		                     &len, NULL, NULL) == CHDERR_NONE) {
			meta[len < sizeof meta ? len : sizeof meta - 1] = '\0';
			sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &tr, type, sub, &frames, &pregap,
			       pgtype, sub, &postgap);
			if (pgtype[0] == 'V') d->first_frame = (uint32_t)pregap;
		}
		sector_kind(d);
		return true;
	}
	if (!(d->f = fopen(file, "rb"))) return false;
	/* A cue's INDEX 01, in sectors until their size is known: once to learn
	 * it, once more from where the track starts */
	sector_kind(d);
	if (idx > 0) {
		d->base = idx * d->sector_size;
		sector_kind(d);
	}
	return true;
}

static void disc_close(disc *d)
{
	if (d->f) fclose(d->f);
	if (d->chd) chd_close(d->chd);
	free(d->hunk);
}

/* ---- ISO 9660, as rcheevos walks it */

typedef struct {
	uint8_t  buf[2048];
	const uint8_t *p;
	uint32_t sector, remaining, block;
	const char *name;
	int name_len;
} walk;

static bool walk_init(walk *w, disc *d)
{
	if (!read_sector(d, 16, w->buf, 256) || memcmp(w->buf, "\1CD001", 6)) return false;
	w->block = w->buf[128] | (w->buf[129] << 8);
	w->sector = w->buf[158] | (w->buf[159] << 8) | ((uint32_t)w->buf[160] << 16);
	w->remaining = w->buf[166] | (w->buf[167] << 8) | ((uint32_t)w->buf[168] << 16) |
	               ((uint32_t)w->buf[169] << 24);
	w->p = NULL;
	return true;
}

static bool walk_next(walk *w, disc *d)
{
	if (!w->p) {
		if (!read_sector(d, w->sector, w->buf, sizeof w->buf)) return false;
		w->p = w->buf;
	}
	for (;;) {
		if (w->p >= w->buf + sizeof w->buf - 34 || !*w->p) {
			if (w->remaining > w->block) {
				w->remaining -= w->block;
				if (read_sector(d, ++w->sector, w->buf, sizeof w->buf)) {
					w->p = w->buf;
					continue;
				}
			}
			return false;
		}
		w->name = (const char *)&w->p[33];
		w->name_len = w->p[32];
		w->p += *w->p;
		if (w->p > w->buf + sizeof w->buf) continue;
		return true;
	}
}

static void entry_of(const walk *w, uint32_t *sector, uint32_t *size)
{
	const uint8_t *e = (const uint8_t *)w->name - 33;
	if (sector) *sector = e[2] | (e[3] << 8) | ((uint32_t)e[4] << 16);
	if (size) *size = e[10] | (e[11] << 8) | ((uint32_t)e[12] << 16) | ((uint32_t)e[13] << 24);
}

static uint32_t find_file(disc *d, const char *path, uint32_t *size)
{
	walk w;
	const char *slash;
	size_t len;

	if (!walk_init(&w, d)) return 0;
	if (*path == '\\') path++;
	while ((slash = strchr(path, '\\'))) {
		bool found = false;
		len = (size_t)(slash - path);
		while (walk_next(&w, d))
			if ((size_t)w.name_len == len && !strncasecmp(w.name, path, len)) {
				entry_of(&w, &w.sector, NULL);
				w.p = NULL;
				found = true;
				break;
			}
		if (!found) return 0;
		path = slash + 1;
	}
	len = strlen(path);
	while (walk_next(&w, d))
		if (((size_t)w.name_len == len || w.name[len] == ';') && !strncasecmp(w.name, path, len)) {
			uint32_t s;
			entry_of(&w, &s, size);
			return s;
		}
	return 0;
}

/* rc_hash_find_playstation_executable, for BOOT and cdrom: */
static uint32_t find_boot(disc *d, char *exe, size_t exe_n, uint32_t *exe_size)
{
	uint8_t buf[2048];
	size_t got;
	uint32_t sector = find_file(d, "SYSTEM.CNF", NULL);
	char *p, *start;

	if (!sector) return 0;
	got = read_sector(d, sector, buf, sizeof buf - 1);
	buf[got] = '\0';
	for (p = (char *)buf; *p; ++p) {
		if (!strncmp(p, "BOOT", 4)) {
			size_t n;
			p += 4;
			while (isspace((unsigned char)*p)) ++p;
			if (*p == '=') {
				++p;
				while (isspace((unsigned char)*p)) ++p;
				if (!strncmp(p, "cdrom:", 6)) p += 6;
				while (*p == '\\') ++p;
				start = p;
				while (*p && !isspace((unsigned char)*p) && *p != ';') ++p;
				n = (size_t)(p - start);
				if (n >= exe_n) n = exe_n - 1;
				memcpy(exe, start, n);
				exe[n] = '\0';
				return find_file(d, exe, exe_size);
			}
		}
		while (*p && *p != '\n') ++p;
		if (!*p) break;
	}
	return 0;
}

bool ra_hash_psx(const char *path, char *out)
{
	disc d;
	char exe[64] = "";
	uint32_t sector, size = 0;
	uint8_t head[32], buf[2048];
	void *md5;
	bool ok = false;

	out[0] = '\0';
	if (!disc_open(&d, path)) return false;
	sector = find_boot(&d, exe, sizeof exe, &size);
	if (!sector && (sector = find_file(&d, "PSX.EXE", &size)))
		memcpy(exe, "PSX.EXE", 8);
	if (sector && read_sector(&d, sector, head, sizeof head) == sizeof head) {
		size_t got;

		if (!memcmp(head, "PS-X EXE", 7))
			size = ((uint32_t)head[31] << 24 | (uint32_t)head[30] << 16 |
			        (uint32_t)head[29] << 8 | head[28]) + 2048;
		if (size > MAX_EXE) size = MAX_EXE;
		ra_md5_begin(&md5);
		ra_md5_add(md5, exe, strlen(exe));
		/* rc_hash_cd_file: whole sectors, the last cut to the size */
		got = read_sector(&d, sector, buf, sizeof buf);
		if (got == sizeof buf) {
			if (size < got) got = size;
			for (;;) {
				ra_md5_add(md5, buf, got);
				if (size <= got) break;
				size -= (uint32_t)got;
				++sector;
				got = read_sector(&d, sector, buf, size >= sizeof buf ? sizeof buf : size);
				if (!got) break;
			}
			ok = true;
		}
		ra_md5_end(md5, out);
		if (!ok) out[0] = '\0';
	}
	disc_close(&d);
	return ok;
}
