/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: cheats - see cheats.h */
#include "cheats.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "artscrape.h"
#include "net.h"

/* ---- reading a .cht ------------------------------------------------------ */

static char *trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t') s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
	return s;
}

/* A value as RetroArch's config files write one: quoted or bare */
static char *unquote(char *v)
{
	size_t n = strlen(v);

	if (n >= 2 && v[0] == '"' && v[n - 1] == '"') { v[n - 1] = '\0'; return v + 1; }
	return v;
}

static bool truthy(const char *v)
{
	return !strcasecmp(v, "true") || !strcmp(v, "1");
}

/* "cheat12_code" -> 12 and "code"; false for anything else */
static bool cheat_key(const char *key, int *num, const char **field)
{
	char *end;
	long  k;

	if (strncmp(key, "cheat", 5) || !isdigit((unsigned char)key[5])) return false;
	k = strtol(key + 5, &end, 10);
	if (*end != '_' || !end[1] || k < 0 || k > 100000) return false;
	*num = (int)k;
	*field = end + 1;
	return true;
}

void cht_free(cht_list *l)
{
	int i;

	if (!l) return;
	for (i = 0; i < l->n; i++) { free(l->e[i].desc); free(l->e[i].code); }
	free(l->e);
	l->e = NULL;
	l->n = 0;
}

bool cht_read(const char *path, cht_list *out)
{
	FILE *f = fopen(path, "r");
	char  line[4096];
	int   n = 0, i;

	memset(out, 0, sizeof *out);
	if (!f) return false;

	/* How many: the declared count, or one past the highest cheat named,
	 * whichever is more - files in the wild disagree with their own count */
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '='), *key;
		const char *field;
		int num;

		if (!eq) continue;
		*eq = '\0';
		key = trim(line);
		if (!strcmp(key, "cheats")) {
			long c = strtol(trim(eq + 1), NULL, 10);
			if (c > n && c < 100000) n = (int)c;
		} else if (cheat_key(key, &num, &field) && num + 1 > n) {
			n = num + 1;
		}
	}
	if (n <= 0) { fclose(f); return false; }
	out->e = calloc((size_t)n, sizeof *out->e);
	if (!out->e) { fclose(f); return false; }
	out->n = n;
	for (i = 0; i < n; i++) { out->e[i].repeat = 1; out->e[i].add_address = 1; }

	rewind(f);
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '='), *key, *val;
		const char *field;
		int num;
		cht_entry *e;

		if (!eq) continue;
		*eq = '\0';
		key = trim(line);
		val = unquote(trim(eq + 1));
		if (!cheat_key(key, &num, &field) || num >= n) continue;
		e = &out->e[num];
		if      (!strcmp(field, "desc"))   { free(e->desc); e->desc = strdup(val); }
		else if (!strcmp(field, "code"))   { free(e->code); e->code = strdup(val); }
		else if (!strcmp(field, "enable")) e->on = truthy(val);
		else if (!strcmp(field, "handler")) e->handler = (int)strtol(val, NULL, 0);
		else if (!strcmp(field, "address")) e->address = (unsigned)strtoul(val, NULL, 0);
		else if (!strcmp(field, "address_bit_position")) e->bitmask = (unsigned)strtoul(val, NULL, 0);
		else if (!strcmp(field, "memory_search_size")) e->size = (unsigned)strtoul(val, NULL, 0);
		else if (!strcmp(field, "cheat_type")) e->type = (unsigned)strtoul(val, NULL, 0);
		else if (!strcmp(field, "value")) e->value = (unsigned)strtoul(val, NULL, 0);
		else if (!strcmp(field, "big_endian")) e->big_endian = truthy(val);
		else if (!strcmp(field, "repeat_count")) e->repeat = (unsigned)strtoul(val, NULL, 0);
		else if (!strcmp(field, "repeat_add_to_address")) e->add_address = (unsigned)strtoul(val, NULL, 0);
		else if (!strcmp(field, "repeat_add_to_value")) e->add_value = (unsigned)strtoul(val, NULL, 0);
	}
	fclose(f);
	return true;
}

bool cht_usable(const cht_entry *e, bool codes_ok)
{
	if (e->handler == 1) return e->type >= 1 && e->type <= 7 && e->size <= 5;
	return codes_ok && e->code && e->code[0] && !strchr(e->code, '?');
}

bool cht_core_takes_codes(const char *core)
{
	/* Beetle NeoPop's retro_cheat_set is empty: measured, a single `ret` */
	return core && strcmp(core, "mednafen_ngp") != 0;
}

/* ---- writing ------------------------------------------------------------- */

bool cht_save_enables(const char *path, const cht_list *l)
{
	FILE *in = fopen(path, "r"), *out;
	char  tmp[1100], line[4096];
	bool *had;
	int   i;

	if (!in) return false;
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	out = fopen(tmp, "w");
	had = calloc((size_t)(l->n > 0 ? l->n : 1), sizeof *had);
	if (!out || !had) { fclose(in); if (out) fclose(out); free(had); return false; }

	while (fgets(line, sizeof line, in)) {
		char copy[4096], *eq, *key;
		const char *field;
		int num;

		snprintf(copy, sizeof copy, "%s", line);
		eq = strchr(copy, '=');
		if (eq) {
			*eq = '\0';
			key = trim(copy);
			if (cheat_key(key, &num, &field) && !strcmp(field, "enable") && num < l->n) {
				fprintf(out, "cheat%d_enable = %s\n", num, l->e[num].on ? "true" : "false");
				had[num] = true;
				continue;
			}
		}
		fputs(line, out);
		if (!strchr(line, '\n')) fputc('\n', out);
	}
	for (i = 0; i < l->n; i++)
		if (!had[i]) fprintf(out, "cheat%d_enable = %s\n", i, l->e[i].on ? "true" : "false");
	fclose(in);
	free(had);
	if (fclose(out) != 0) { remove(tmp); return false; }
	return rename(tmp, path) == 0;
}

int cht_write_active(const char *path, const cht_list *l, bool codes_ok)
{
	FILE *f = fopen(path, "w");
	int   i, n = 0;

	if (!f) return -1;
	for (i = 0; l && i < l->n; i++) {
		const cht_entry *e = &l->e[i];

		if (!e->on || !cht_usable(e, codes_ok)) continue;
		if (e->handler == 1)
			fprintf(f, "M\t%u %u %u %u %u %u %u %u %u\n", e->address, e->size, e->type, e->value,
			        e->big_endian ? 1u : 0u, e->bitmask, e->repeat, e->add_address, e->add_value);
		else
			fprintf(f, "C\t%s\n", e->code);
		n++;
	}
	return fclose(f) == 0 ? n : -1;
}

void cht_path(const char *card, const char *folder, const char *rom_file, char *out, size_t n)
{
	char stem[512];
	char *dot;

	snprintf(stem, sizeof stem, "%s", rom_file);
	dot = strrchr(stem, '.');
	if (dot && dot != stem) *dot = '\0';
	snprintf(out, n, "%s/Cheats/%s/%s.cht", card, folder, stem);
}

/* ---- the index ----------------------------------------------------------- */

void cht_index_free(cht_index *x)
{
	int i;

	if (!x) return;
	for (i = 0; i < x->n; i++) free(x->names[i]);
	free(x->names);
	memset(x, 0, sizeof *x);
}

bool cht_index_parse(const char *text, cht_index *out)
{
	const char *p = text, *nl;
	int cap = 0;

	memset(out, 0, sizeof *out);
	if (!text || strncmp(text, "# ", 2)) return false;
	nl = strchr(p, '\n');
	if (!nl) return false;
	snprintf(out->commit, sizeof out->commit, "%.*s", (int)(nl - p - 2), p + 2);
	out->commit[strcspn(out->commit, " \r\t")] = '\0';
	if (strlen(out->commit) < 7) return false;
	for (p = nl + 1; *p; p = nl ? nl + 1 : p + strlen(p)) {
		size_t len;

		nl = strchr(p, '\n');
		len = nl ? (size_t)(nl - p) : strlen(p);
		if (len && p[len - 1] == '\r') len--;
		if (!len || *p == '#') { if (!nl) break; continue; }
		/* "(Rumbles)" files only shake a controller, which the Brick has not */
		if (memmem(p, len, "(Rumbles)", 9)) { if (!nl) break; continue; }
		if (out->n == cap) {
			char **g;
			cap = cap ? cap * 2 : 256;
			g = realloc(out->names, (size_t)cap * sizeof *g);
			if (!g) { cht_index_free(out); return false; }
			out->names = g;
		}
		out->names[out->n] = strndup(p, len);
		if (!out->names[out->n]) { cht_index_free(out); return false; }
		out->n++;
		if (!nl) break;
	}
	return true;
}

static void stem_of(const char *rom_file, char *out, size_t n)
{
	char *dot;

	snprintf(out, n, "%s", rom_file);
	dot = strrchr(out, '.');
	if (dot && dot != out) *dot = '\0';
}

int cht_candidates(const cht_index *x, const char *rom_file, int *idx, int max)
{
	char stem[512], want[512], have[512];
	int  i, j, n = 0, pass;

	stem_of(rom_file, stem, sizeof stem);
	/* The same title, then - only if that found none - the loose one */
	for (pass = 0; pass < 2 && n == 0; pass++) {
		if (pass == 0) art_norm(stem, want, sizeof want);
		else           art_norm_loose(stem, want, sizeof want);
		if (!want[0]) continue;
		for (i = 0; i < x->n && n < max; i++) {
			if (pass == 0) art_norm(x->names[i], have, sizeof have);
			else           art_norm_loose(x->names[i], have, sizeof have);
			if (strcmp(want, have)) continue;
			idx[n++] = i;
		}
	}
	/* Best first: the exact name, then the tags that match the game's */
	for (i = 1; i < n; i++) {
		int k = idx[i], ks = strcmp(x->names[k], stem) ? art_tag_score(stem, x->names[k]) : 1000;
		for (j = i; j > 0; j--) {
			int p = idx[j - 1], ps = strcmp(x->names[p], stem) ? art_tag_score(stem, x->names[p]) : 1000;
			if (ps >= ks) break;
			idx[j] = p;
		}
		idx[j] = k;
	}
	return n;
}

int cht_nearest(const cht_index *x, const char *rom_file)
{
	char stem[512];
	int  i;

	stem_of(rom_file, stem, sizeof stem);
	for (i = 0; i < x->n; i++)
		if (strcasecmp(x->names[i], stem) >= 0) return i;
	return x->n > 0 ? x->n - 1 : 0;
}

void cht_index_url(const char *collection, char *out, size_t n)
{
	char enc[512];

	net_urlencode(collection, enc, sizeof enc);
	snprintf(out, n, "%s%s.txt", CHT_SITE, enc);
}

void cht_file_url(const cht_index *x, const char *collection, const char *name,
                  char *out, size_t n)
{
	char c[512], f[1024];

	net_urlencode(collection, c, sizeof c);
	net_urlencode(name, f, sizeof f);
	snprintf(out, n, "%s%s/cht/%s/%s.cht", CHT_RAW, x->commit, c, f);
}
