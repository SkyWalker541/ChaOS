/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: PICO-8 - see pico8.h */
#include "pico8.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static bool has_file(const char *dir, const char *name)
{
	char p[1024];
	struct stat st;

	snprintf(p, sizeof p, "%s/%s", dir, name);
	return stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

int p8_elf_arm(const char *path)
{
	unsigned char h[20];
	FILE *f = fopen(path, "rb");
	size_t got;
	unsigned m;

	if (!f) return -1;
	got = fread(h, 1, sizeof h, f);
	fclose(f);
	if (got < sizeof h || memcmp(h, "\177ELF", 4)) return -1;
	/* e_machine, in the file's own byte order */
	m = h[5] == 2 ? (unsigned)(h[18] << 8 | h[19]) : (unsigned)(h[18] | h[19] << 8);
	if (h[4] == 2 && m == 183) return 64;   /* ELFCLASS64, EM_AARCH64 */
	if (h[4] == 1 && m == 40) return 32;    /* ELFCLASS32, EM_ARM */
	return -1;
}

p8_status p8_check(const char *dir)
{
	bool bin = has_file(dir, "pico8_64"), dat = has_file(dir, "pico8.dat");
	char p[1024];

	if (!bin && !dat) return P8_NONE;
	if (!dat) return P8_NO_DAT;
	if (!bin) return P8_NO_BIN;
	snprintf(p, sizeof p, "%s/pico8_64", dir);
	return p8_elf_arm(p) == 64 ? P8_READY : P8_WRONG_CPU;
}

const char *p8_status_say(p8_status s)
{
	switch (s) {
	case P8_READY:     return "Ready";
	case P8_NONE:      return "Put pico8_64 and pico8.dat in PICO-8";
	case P8_NO_DAT:    return "pico8.dat is missing from PICO-8";
	case P8_NO_BIN:    return "pico8_64 is missing from PICO-8";
	case P8_WRONG_CPU: return "That pico8_64 isn't the Raspberry Pi one";
	}
	return "";
}

static bool ends_with(const char *s, const char *tail)
{
	size_t a = strlen(s), b = strlen(tail);

	return a >= b && !strcasecmp(s + a - b, tail);
}

bool p8_find_zip(const char *dir, char *out, size_t n)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char best[256] = "";

	if (!d) return false;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !ends_with(e->d_name, "_raspi.zip")) continue;
		if (strncasecmp(e->d_name, "pico-8", 6) && strncasecmp(e->d_name, "pico8", 5)) continue;
		/* pico-8_0.2.6b_raspi.zip after pico-8_0.2.5g_raspi.zip */
		if (!best[0] || strcasecmp(e->d_name, best) > 0)
			snprintf(best, sizeof best, "%s", e->d_name);
	}
	closedir(d);
	if (!best[0]) return false;
	snprintf(out, n, "%s/%s", dir, best);
	return true;
}

/* The first file called `name` under `dir`, a few folders down at most */
static bool find_under(const char *dir, const char *name, char *out, size_t n, int depth)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	bool found = false;

	if (!d) return false;
	while (!found && (e = readdir(d))) {
		char p[1024];
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		if (stat(p, &st) != 0) continue;
		if (S_ISREG(st.st_mode) && !strcmp(e->d_name, name)) {
			snprintf(out, n, "%s", p);
			found = true;
		} else if (S_ISDIR(st.st_mode) && depth > 0) {
			found = find_under(p, name, out, n, depth - 1);
		}
	}
	closedir(d);
	return found;
}

/* rename, or a copy where rename can't (another file system) */
static bool move_file(const char *from, const char *to)
{
	FILE *in, *out;
	char buf[65536];
	size_t got;
	bool ok = true;

	unlink(to);
	if (rename(from, to) == 0) return true;
	if (!(in = fopen(from, "rb"))) return false;
	if (!(out = fopen(to, "wb"))) { fclose(in); return false; }
	while ((got = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, got, out) != got) { ok = false; break; }
	fclose(in);
	if (fclose(out) != 0) ok = false;
	if (ok) unlink(from);
	else unlink(to);
	return ok;
}

bool p8_take_from(const char *stage, const char *dir)
{
	static const char *names[] = { "pico8_64", "pico8.dat" };
	char from[2][1024], to[1024];
	int i;

	for (i = 0; i < 2; i++)
		if (!find_under(stage, names[i], from[i], sizeof from[i], 3)) return false;
	for (i = 0; i < 2; i++) {
		snprintf(to, sizeof to, "%s/%s", dir, names[i]);
		if (!move_file(from[i], to)) return false;
		chmod(to, 0755);
	}
	return true;
}

bool p8_is_cart(const char *file)
{
	return ends_with(file, ".p8") || ends_with(file, ".p8.png");
}
