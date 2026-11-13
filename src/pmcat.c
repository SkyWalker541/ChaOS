/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: PortMaster's catalog - see pmcat.h */
#include "pmcat.h"
#include "rajson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long sz;

	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	buf = malloc((size_t)sz + 1);
	if (buf && fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); buf = NULL; }
	fclose(f);
	if (!buf) return NULL;
	buf[sz] = '\0';
	*len = (size_t)sz;
	return buf;
}

/* A list of strings, joined with ", " */
static void join(jsv arr, char *out, size_t n)
{
	jsv it = { 0 }, e;
	char one[96];
	size_t at = 0;

	out[0] = '\0';
	while (js_next(arr, &it, &e)) {
		if (!js_str(e, one, sizeof one) || !one[0]) continue;
		at += (size_t)snprintf(out + at, at < n ? n - at : 0, "%s%s", at ? ", " : "", one);
		if (at >= n) { out[n - 1] = '\0'; break; }
	}
}

static bool has_str(jsv arr, const char *want)
{
	jsv it = { 0 }, e;
	char one[64];

	while (js_next(arr, &it, &e))
		if (js_str(e, one, sizeof one) && !strcasecmp(one, want)) return true;
	return false;
}

/* What it needs, against the Brick. NULL: nothing rules it out. */
static const char *ruled_out(jsv attr, unsigned *flags)
{
	jsv reqs, arch, it = { 0 }, e;
	char r[64];
	const char *why = NULL;

	if (js_member(attr, "reqs", &reqs)) {
		while (js_next(reqs, &it, &e)) {
			if (!js_str(e, r, sizeof r)) continue;
			/* "a|b": any one of them will do */
			if (strchr(r, '|')) {
				if (strstr(r, "analog_1")) continue;
				if (!why) why = "Needs two analog sticks";
				continue;
			}
			if (!strcasecmp(r, "power"))    { *flags |= PMF_POWER; continue; }
			if (!strcasecmp(r, "analog_2")) { if (!why) why = "Needs two analog sticks"; continue; }
			if (!strcasecmp(r, "2gb"))      { if (!why) why = "Needs 2 GB of memory"; continue; }
			if (!strcasecmp(r, "opengl"))   { if (!why) why = "Needs OpenGL"; continue; }
			if (!strcasecmp(r, "!trimui"))  { if (!why) why = "Doesn't run on TrimUI devices"; continue; }
			if (!strcasecmp(r, "ultra"))    { if (!why) why = "Needs a much stronger device"; continue; }
			if (!strcasecmp(r, "armhf"))    { if (!why) why = "Only built for 32-bit ARM"; continue; }
		}
	}
	if (js_member(attr, "arch", &arch)) {
		jsv i2 = { 0 }, a2;
		bool any = false;

		while (js_next(arch, &i2, &a2)) any = true;
		if (any && !has_str(arch, "aarch64") && !why) why = "Not built for the Brick's processor";
	}
	return why;
}

static int read_version(const char *ports_dir, const char *dir)
{
	char path[600];
	size_t len = 0;
	char *doc;
	jsv v;
	int ver = -1;

	snprintf(path, sizeof path, "%s/%s/port.json", ports_dir, dir);
	doc = slurp(path, &len);
	if (!doc) return -1;
	if (js_member(js_root(doc, len), "version", &v)) ver = (int)js_int(v);
	else ver = 0;
	free(doc);
	return ver;
}

void pmcat_installed(pm_catalog *c, const char *ports_dir)
{
	int i;

	for (i = 0; i < c->n; i++) {
		pm_port *p = &c->p[i];
		char sh[600];
		struct stat st;
		int v = -1;

		p->flags &= ~(unsigned)(PMF_INSTALLED | PMF_UPDATE);
		p->have_version = 0;
		if (!ports_dir || !p->dir[0]) continue;
		v = read_version(ports_dir, p->dir);
		if (v < 0) {
			/* no port.json: still installed if its script and folder are */
			char d[600];

			snprintf(sh, sizeof sh, "%s/%s", ports_dir, p->script);
			snprintf(d, sizeof d, "%s/%s", ports_dir, p->dir);
			if (!p->script[0] || stat(sh, &st) != 0 || stat(d, &st) != 0) continue;
			v = 0;
		}
		p->flags |= PMF_INSTALLED;
		p->have_version = v;
		if (v < p->version) p->flags |= PMF_UPDATE;
	}
}

static void load_tested(pm_catalog *c, const char *tested)
{
	FILE *f = tested ? fopen(tested, "r") : NULL;
	char line[128];

	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *e = line + strcspn(line, "\r\n# \t");
		int i;

		*e = '\0';
		if (!line[0]) continue;
		i = pmcat_find(c, line);
		if (i >= 0) c->p[i].flags |= PMF_TESTED;
	}
	fclose(f);
}

bool pmcat_load(pm_catalog *c, const char *path, const char *ports_dir, const char *tested)
{
	jsv root, ports, utils, it = { 0 }, key, val;
	int cap = 0;

	memset(c, 0, sizeof *c);
	c->doc = slurp(path, &c->len);
	if (!c->doc) return false;
	root = js_root(c->doc, c->len);
	if (!js_member(root, "ports", &ports)) { pmcat_free(c); return false; }
	if (js_member(root, "utils", &utils)) { c->utils_b = utils.b; c->utils_e = utils.e; }

	while (js_next_member(ports, &it, &key, &val)) {
		pm_port *p;
		jsv attr, src = { 0 }, x, items, ii = { 0 }, item;
		char s[128];

		if (c->n == cap) {
			pm_port *np;

			cap = cap ? cap * 2 : 1024;
			np = realloc(c->p, (size_t)cap * sizeof *np);
			if (!np) break;
			c->p = np;
		}
		p = &c->p[c->n];
		memset(p, 0, sizeof *p);
		snprintf(p->zip, sizeof p->zip, "%.*s", (int)(key.e - key.b), key.b);
		if (!js_member(val, "attr", &attr)) continue;
		p->attr_b = attr.b; p->attr_e = attr.e;
		if (js_member(val, "source", &src)) { p->src_b = src.b; p->src_e = src.e; }
		if (js_member(val, "version", &x)) p->version = (int)js_int(x);
		if (js_member(attr, "title", &x)) js_str(x, p->title, sizeof p->title);
		if (!p->title[0]) snprintf(p->title, sizeof p->title, "%s", p->zip);
		if (js_member(attr, "porter", &x)) join(x, p->porter, sizeof p->porter);
		if (js_member(attr, "genres", &x)) join(x, p->genres, sizeof p->genres);
		if (js_member(attr, "rtr", &x) && js_is_true(x)) p->flags |= PMF_RTR;
		if (js_member(attr, "exp", &x) && js_is_true(x)) p->flags |= PMF_EXP;
		if (js_member(attr, "runtime", &x)) {
			jsv ri = { 0 }, r;

			while (js_next(x, &ri, &r))
				if (js_str(r, s, sizeof s) && s[0]) p->flags |= PMF_RUNTIME;
		}
		if (js_member(attr, "image", &x)) {
			jsv shot;

			if (js_member(x, "screenshot", &shot)) js_str(shot, p->shot, sizeof p->shot);
		}
		if (js_member(val, "items", &items)) {
			while (js_next(items, &ii, &item)) {
				size_t l;

				if (!js_str(item, s, sizeof s) || !s[0]) continue;
				l = strlen(s);
				while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\t')) s[--l] = '\0';
				if (l > 3 && !strcasecmp(s + l - 3, ".sh")) {
					if (!p->script[0]) snprintf(p->script, sizeof p->script, "%.95s", s);
				} else if (!p->dir[0]) {
					while (l > 0 && s[l - 1] == '/') s[--l] = '\0';
					if (!strchr(s, '/')) snprintf(p->dir, sizeof p->dir, "%.63s", s);
				}
			}
		}
		if (src.b && js_member(src, "size", &x)) p->size = js_int(x);
		if (src.b && js_member(src, "date_added", &x) && js_str(x, s, sizeof s)) {
			int y, m, d;

			if (sscanf(s, "%d-%d-%d", &y, &m, &d) == 3) p->added = y * 10000 + m * 100 + d;
		}
		p->why = ruled_out(attr, &p->flags);
		if (p->why) p->flags |= PMF_NO;
		c->n++;
	}
	if (c->n == 0) { pmcat_free(c); return false; }
	load_tested(c, tested);
	pmcat_installed(c, ports_dir);
	return true;
}

void pmcat_free(pm_catalog *c)
{
	free(c->doc);
	free(c->p);
	memset(c, 0, sizeof *c);
}

bool pmcat_in(const pm_port *p, pm_section s)
{
	switch (s) {
	case PMS_TESTED:    return (p->flags & PMF_TESTED) != 0;
	case PMS_MAY:       return !(p->flags & PMF_NO);
	case PMS_ALL:       return true;
	case PMS_INSTALLED: return (p->flags & PMF_INSTALLED) != 0;
	case PMS_UPDATES:   return (p->flags & PMF_UPDATE) != 0;
	default:            return false;
	}
}

static const pm_catalog *g_sort;

static int by_rtr_title(const void *a, const void *b)
{
	const pm_port *x = &g_sort->p[*(const int *)a], *y = &g_sort->p[*(const int *)b];
	int rx = !(x->flags & PMF_RTR), ry = !(y->flags & PMF_RTR);

	if (rx != ry) return rx - ry;
	return strcasecmp(x->title, y->title);
}

static bool has_genre(const pm_port *p, const char *genre)
{
	const char *g = p->genres;
	size_t n = strlen(genre);

	while (*g) {
		if (!strncasecmp(g, genre, n) && (g[n] == '\0' || g[n] == ',')) return true;
		g = strchr(g, ',');
		if (!g) break;
		g += 2;
	}
	return false;
}

int pmcat_list(const pm_catalog *c, pm_section s, const char *genre, int *out, int max)
{
	int i, n = 0, *all = malloc((size_t)(c->n ? c->n : 1) * sizeof *all);

	if (!all) return 0;
	for (i = 0; i < c->n; i++) {
		if (!pmcat_in(&c->p[i], s)) continue;
		if (genre && genre[0] && !has_genre(&c->p[i], genre)) continue;
		all[n++] = i;
	}
	g_sort = c;
	qsort(all, (size_t)n, sizeof *all, by_rtr_title);
	for (i = 0; i < n && i < max; i++) out[i] = all[i];
	free(all);
	return n;
}

int pmcat_genres(const pm_catalog *c, char out[][24], int max)
{
	char names[32][24];
	int count[32], ng = 0, i, j;

	for (i = 0; i < c->n; i++) {
		const char *g = c->p[i].genres;

		while (*g) {
			size_t l = strcspn(g, ",");
			char one[24];

			snprintf(one, sizeof one, "%.*s", (int)l, g);
			for (j = 0; j < ng && strcasecmp(names[j], one); j++) ;
			if (j == ng && ng < 32) { snprintf(names[ng], 24, "%s", one); count[ng++] = 0; }
			if (j < ng) count[j]++;
			g += l;
			while (*g == ',' || *g == ' ') g++;
		}
	}
	/* most first */
	for (i = 0; i < ng; i++)
		for (j = i + 1; j < ng; j++)
			if (count[j] > count[i]) {
				char t[24];
				int k = count[i];

				memcpy(t, names[i], 24); memcpy(names[i], names[j], 24); memcpy(names[j], t, 24);
				count[i] = count[j]; count[j] = k;
			}
	for (i = 0; i < ng && i < max; i++) memcpy(out[i], names[i], 24);
	return ng;
}

bool pmcat_text(const pm_port *p, const char *field, char *out, size_t n)
{
	jsv attr = { p->attr_b, p->attr_e }, x;

	out[0] = '\0';
	return p->attr_b && js_member(attr, field, &x) && js_str(x, out, n) && out[0];
}

bool pmcat_source(const pm_port *p, char *url, size_t un, char *md5, size_t mn)
{
	jsv src = { p->src_b, p->src_e }, x;

	url[0] = md5[0] = '\0';
	if (!p->src_b) return false;
	if (js_member(src, "md5", &x)) js_str(x, md5, mn);
	return js_member(src, "url", &x) && js_str(x, url, un) && url[0];
}

int pmcat_runtimes(const pm_port *p, char out[][64], int max)
{
	jsv attr = { p->attr_b, p->attr_e }, rt, it = { 0 }, e;
	int n = 0;

	if (!p->attr_b || !js_member(attr, "runtime", &rt)) return 0;
	while (js_next(rt, &it, &e)) {
		char s[64];
		size_t l;

		if (!js_str(e, s, sizeof s) || !s[0] || strchr(s, '/')) continue;
		l = strlen(s);
		if (l < 9 || strcasecmp(s + l - 9, ".squashfs"))
			strncat(s, ".squashfs", sizeof s - l - 1);
		if (n < max) snprintf(out[n], 64, "%s", s);
		n++;
	}
	return n < max ? n : max;
}

static bool util_entry(const pm_catalog *c, const char *name, jsv *out)
{
	jsv u = { c->utils_b, c->utils_e };

	return c->utils_b && js_member(u, name, out);
}

bool pmcat_runtime(const pm_catalog *c, const char *name, char *url, size_t un,
                   char *md5, size_t mn, long *size)
{
	jsv e, x;
	char alt[96];
	size_t l = strlen(name);

	url[0] = md5[0] = '\0';
	*size = 0;
	if (!util_entry(c, name, &e)) {
		/* "x.squashfs" can be in the catalog as "x.aarch64.squashfs" */
		if (l <= 9 || l + 8 >= sizeof alt) return false;
		snprintf(alt, sizeof alt, "%.*s.aarch64.squashfs", (int)(l - 9), name);
		if (!util_entry(c, alt, &e)) return false;
	}
	if (js_member(e, "md5", &x)) js_str(x, md5, mn);
	if (js_member(e, "size", &x)) *size = js_int(x);
	return js_member(e, "url", &x) && js_str(x, url, un) && url[0];
}

bool pmcat_shot(const pm_port *p, char *url, size_t un, char *file, size_t fn)
{
	const char *ext;
	size_t zl = strlen(p->zip);

	if (!p->shot[0] || zl <= 4) return false;
	ext = strrchr(p->shot, '.');
	snprintf(url, un, PMCAT_IMG_BASE "%.*s/%s", (int)(zl - 4), p->zip, p->shot);
	snprintf(file, fn, "%.*s%s", (int)(zl - 4), p->zip, ext ? ext : ".png");
	return true;
}

int pmcat_find(const pm_catalog *c, const char *zip)
{
	int i;

	for (i = 0; i < c->n; i++)
		if (!strcasecmp(c->p[i].zip, zip)) return i;
	return -1;
}

/* ---- manifests ----------------------------------------------------------- */

static unsigned rd16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned long rd32(const unsigned char *p)
{
	return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

bool pmcat_zip_names(const char *zip, const char *out)
{
	FILE *f = fopen(zip, "rb"), *o = NULL;
	unsigned char tail[65557], h[46];
	char name[1024], tmp[1100];
	long size, from;
	size_t got;
	unsigned long cd_off, i, count;
	int at;
	bool ok = false;

	if (!f) return false;
	if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 22) goto done;
	from = size > (long)sizeof tail ? size - (long)sizeof tail : 0;
	if (fseek(f, from, SEEK_SET) != 0) goto done;
	got = fread(tail, 1, sizeof tail, f);
	for (at = (int)got - 22; at >= 0; at--)
		if (tail[at] == 'P' && tail[at + 1] == 'K' && tail[at + 2] == 5 && tail[at + 3] == 6) break;
	if (at < 0) goto done;
	count = rd16(tail + at + 10);
	cd_off = rd32(tail + at + 16);
	if (count == 0xFFFF || cd_off == 0xFFFFFFFFul) goto done;   /* zip64 */
	if (fseek(f, (long)cd_off, SEEK_SET) != 0) goto done;
	snprintf(tmp, sizeof tmp, "%s.new", out);
	if (!(o = fopen(tmp, "w"))) goto done;
	for (i = 0; i < count; i++) {
		unsigned nl, xl, cl;

		if (fread(h, 1, 46, f) != 46 || rd32(h) != 0x02014b50ul) goto done;
		nl = rd16(h + 28); xl = rd16(h + 30); cl = rd16(h + 32);
		if (nl >= sizeof name || fread(name, 1, nl, f) != nl) goto done;
		name[nl] = '\0';
		if (fseek(f, (long)(xl + cl), SEEK_CUR) != 0) goto done;
		if (!strchr(name, '\n')) fprintf(o, "%s\n", name);
	}
	ok = fclose(o) == 0;
	o = NULL;
	if (ok) ok = rename(tmp, out) == 0;
	if (!ok) unlink(tmp);
done:
	if (o) { fclose(o); unlink(tmp); }
	fclose(f);
	return ok;
}

static bool safe_name(const char *n)
{
	const char *p;

	if (!n[0] || n[0] == '/' || n[0] == '\\') return false;
	for (p = n; *p; p++) {
		if (p[0] == '.' && p[1] == '.' && (p == n || p[-1] == '/') && (p[2] == '/' || !p[2]))
			return false;
		if (*p == '\\') return false;
	}
	return true;
}

static int by_len_desc(const void *a, const void *b)
{
	size_t x = strlen(*(char *const *)a), y = strlen(*(char *const *)b);

	return x < y ? 1 : x > y ? -1 : 0;
}

int pmcat_manifest_remove(const char *ports_dir, const char *manifest)
{
	FILE *f = fopen(manifest, "r");
	char line[1100], path[1700];
	char **dirs = NULL;
	int nd = 0, cap = 0, gone = 0, i;

	if (!f) return 0;
	while (fgets(line, sizeof line, f)) {
		size_t l = strcspn(line, "\r\n");
		struct stat st;

		line[l] = '\0';
		if (!safe_name(line)) continue;
		snprintf(path, sizeof path, "%s/%s", ports_dir, line);
		/* every folder a name sits in, so the empty ones go too */
		{
			char *sl;

			while (l > 0 && line[l - 1] == '/') line[--l] = '\0';
			for (;;) {
				bool seen = false;

				if (l && lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
					for (i = 0; i < nd && !seen; i++) seen = !strcmp(dirs[i], line);
					if (!seen) {
						if (nd == cap) {
							char **nds = realloc(dirs, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *dirs);

							if (!nds) break;
							dirs = nds;
						}
						dirs[nd++] = strdup(line);
					}
				}
				sl = strrchr(line, '/');
				if (!sl) break;
				*sl = '\0';
				l = strlen(line);
				snprintf(path, sizeof path, "%s/%s", ports_dir, line);
			}
		}
		snprintf(path, sizeof path, "%s/%s", ports_dir, line);
		/* reread: `line` was cut down to its folders above */
	}
	/* the files: a second pass, now the folders are known */
	rewind(f);
	while (fgets(line, sizeof line, f)) {
		size_t l = strcspn(line, "\r\n");
		struct stat st;

		line[l] = '\0';
		if (!safe_name(line) || (l && line[l - 1] == '/')) continue;
		snprintf(path, sizeof path, "%s/%s", ports_dir, line);
		if (lstat(path, &st) == 0 && !S_ISDIR(st.st_mode) && unlink(path) == 0) gone++;
	}
	fclose(f);
	qsort(dirs, (size_t)nd, sizeof *dirs, by_len_desc);
	for (i = 0; i < nd; i++) {
		if (dirs[i]) {
			snprintf(path, sizeof path, "%s/%s", ports_dir, dirs[i]);
			rmdir(path);   /* only if empty: what the port made stays */
		}
		free(dirs[i]);
	}
	free(dirs);
	return gone;
}
