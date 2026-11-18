/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: games downloaded from itch.io. See itchdl.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "itchdl.h"
#include "net.h"
#include "rajson.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* Only a test build points these elsewhere */
#ifndef ITCHDL_API
#define ITCHDL_API "https://api.itch.io"
#endif

#define PAGE_MAX (3 << 20)    /* a game's page runs to a few hundred KB */

/* ---- the key ------------------------------------------------------------- */

static void key_path(const char *dir, char *out, size_t n)
{
	snprintf(out, n, "%s/itch.key", dir);
}

bool itchdl_key_load(const char *dir, char *key, size_t n)
{
	char p[512];
	FILE *f;
	size_t l;

	key[0] = '\0';
	key_path(dir, p, sizeof p);
	if (!(f = fopen(p, "r"))) return false;
	if (!fgets(key, (int)n, f)) key[0] = '\0';
	fclose(f);
	l = strcspn(key, "\r\n \t");
	key[l] = '\0';
	return key[0] != '\0';
}

bool itchdl_key_save(const char *dir, const char *key)
{
	char p[512], t[520];
	int fd;
	size_t l;

	key_path(dir, p, sizeof p);
	if (!key || !key[0]) return unlink(p) == 0 || errno == ENOENT;
	mkdir(dir, 0700);
	snprintf(t, sizeof t, "%s.part", p);
	fd = open(t, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return false;
	l = strlen(key);
	if (write(fd, key, l) != (ssize_t)l || write(fd, "\n", 1) != 1 || fsync(fd) != 0) {
		close(fd);
		unlink(t);
		return false;
	}
	close(fd);
	return rename(t, p) == 0;
}

/* ---- requests ------------------------------------------------------------- */

/* A page or a reply into a buffer of its own: the body, NUL-terminated, or
 * NULL when nothing answered. *http gets the status. */
static char *ask(const char *url, const char *bearer, const char *jar, const net_field *f, int nf,
                 bool html, int *http)
{
	char *buf = malloc(PAGE_MAX);
	net_req r;
	long got;

	*http = 0;
	if (!buf) return NULL;
	memset(&r, 0, sizeof r);
	r.url = url;
	r.bearer = bearer;
	r.cookies = jar && jar[0] ? jar : NULL;
	r.fields = f;
	r.nfields = nf;
	r.accept = html ? "text/html,application/xhtml+xml" : NULL;
	r.timeout_s = 30;
	got = net_request(&r, buf, PAGE_MAX, http);
	if (got < 0) { free(buf); return NULL; }
	return buf;
}

/* `errors`' first line, when a reply has one */
static bool reply_error(const char *body, char *why, size_t wn)
{
	jsv root = js_root(body, strlen(body)), errs, it = { 0 }, e;
	char msg[160];

	if (!js_member(root, "errors", &errs) || !js_next(errs, &it, &e) || !js_str(e, msg, sizeof msg))
		return false;
	snprintf(why, wn, "itch.io: %s", msg);
	return true;
}

static void say_http(int http, char *why, size_t wn)
{
	if (http == 0) snprintf(why, wn, "Couldn't reach itch.io");
	else if (http == 404 || http == 410) snprintf(why, wn, "It's gone from itch.io");
	else if (http == 429) snprintf(why, wn, "itch.io asked to wait; try later");
	else snprintf(why, wn, "itch.io didn't answer (%d)", http);
}

int itchdl_key_check(const char *key, char *user, size_t n)
{
	int http;
	char *b = ask(ITCHDL_API "/profile", key, NULL, NULL, 0, false, &http);
	jsv root, u, name;
	int r = -1;

	if (user && n) user[0] = '\0';
	if (!b) return -1;
	if (http == 401 || http == 403) r = 0;
	else if (http == 200) {
		root = js_root(b, strlen(b));
		if (js_member(root, "user", &u) && js_member(u, "username", &name)) {
			if (user) js_str(name, user, n);
			r = 1;
		} else {
			r = 0;
		}
	}
	free(b);
	return r;
}

/* ---- reading pages ---------------------------------------------------------- */

bool itchdl_csrf(const char *html, char *out, size_t n)
{
	const char *p = html;

	out[0] = '\0';
	while ((p = strstr(p, "name=\"csrf_token\""))) {
		const char *a = p + 17, *v, *q;

		while (*a == ' ' || *a == '\t' || *a == '\n') a++;
		if (!strncmp(a, "value=\"", 7)) v = a + 7;
		else if (!strncmp(a, "content=\"", 9)) v = a + 9;
		else { p = a; continue; }
		q = strchr(v, '"');
		if (!q || (size_t)(q - v) >= n || q == v) { p = a; continue; }
		memcpy(out, v, (size_t)(q - v));
		out[q - v] = '\0';
		return true;
	}
	return false;
}

/* An attribute's value inside [tag, end) */
static bool attr(const char *tag, const char *end, const char *name, char *out, size_t n)
{
	char pat[48];
	const char *p, *v, *q;

	snprintf(pat, sizeof pat, "%s=\"", name);
	p = memmem(tag, (size_t)(end - tag), pat, strlen(pat));
	if (!p) return false;
	v = p + strlen(pat);
	q = memchr(v, '"', (size_t)(end - v));
	if (!q || (size_t)(q - v) >= n) return false;
	memcpy(out, v, (size_t)(q - v));
	out[q - v] = '\0';
	return true;
}

/* &amp; and its friends in a file name, which a title attribute carries */
static void unentity(char *s)
{
	static const struct { const char *e; char c; } E[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&#39;", '\'' },
		{ "&#x27;", '\'' },
	};
	char *r = s, *w = s;

	while (*r) {
		size_t i;
		bool hit = false;

		if (*r == '&')
			for (i = 0; i < sizeof E / sizeof E[0]; i++) {
				size_t l = strlen(E[i].e);

				if (!strncmp(r, E[i].e, l)) { *w++ = E[i].c; r += l; hit = true; break; }
			}
		if (!hit) *w++ = *r++;
	}
	*w = '\0';
}

/* One file from a block of the page: its data-upload_id and its
 * <strong class="name">'s title (or text), in either order (itch.io puts the
 * button first; the pak's sample puts it last) */
static bool upload_from(const char *b, const char *e, itchdl_upload *u)
{
	const char *a = memmem(b, (size_t)(e - b), "data-upload_id=\"", 16), *s = b, *name = NULL, *te = NULL;
	const char *v, *q;

	memset(u, 0, sizeof *u);
	if (!a) return false;
	v = a + 16;
	q = memchr(v, '"', (size_t)(e - v));
	if (!q || q == v || (size_t)(q - v) >= sizeof u->id) return false;
	memcpy(u->id, v, (size_t)(q - v));
	while ((s = memmem(s, (size_t)(e - s), "<strong", 7))) {
		const char *t = memchr(s, '>', (size_t)(e - s));

		if (!t) break;
		if (memmem(s, (size_t)(t - s), "name", 4)) { name = s; te = t; break; }
		s = t;
	}
	if (!name) return false;
	if (!attr(name, te, "title", u->name, sizeof u->name)) {
		const char *t = te + 1, *c = memchr(t, '<', (size_t)(e - t));

		if (c) {
			while (t < c && isspace((unsigned char)*t)) t++;
			while (c > t && isspace((unsigned char)c[-1])) c--;
			if ((size_t)(c - t) < sizeof u->name) { memcpy(u->name, t, (size_t)(c - t)); u->name[c - t] = '\0'; }
		}
	}
	unentity(u->name);
	return u->name[0] && strspn(u->id, "0123456789") == strlen(u->id);
}

/* The next <div class="upload ..."> (the class "upload", alone or first) */
static const char *upload_div(const char *p)
{
	while ((p = strstr(p, "<div class=\"upload"))) {
		char c = p[18];

		if (c == '"' || c == ' ') return p;
		p += 18;
	}
	return NULL;
}

int itchdl_parse_page(const char *html, itchdl_upload *out, int max)
{
	const char *p = html;
	int n = 0;

	/* By block: <div class="upload"> to the next one (or the page's end) */
	while (n < max && (p = upload_div(p))) {
		const char *next = upload_div(p + 1), *e = next ? next : p + strlen(p);
		itchdl_upload u;
		int i;
		bool dup = false;

		if (upload_from(p, e, &u)) {
			for (i = 0; i < n; i++) if (!strcmp(out[i].id, u.id)) dup = true;
			if (!dup) out[n++] = u;
		}
		p = e;
	}
	return n;
}

static int b64v(int c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+' || c == '-') return 62;
	if (c == '/' || c == '_') return 63;
	return -1;
}

/* %XX in a URL's part */
static void unpercent(char *s)
{
	char *r = s, *w = s;

	while (*r) {
		if (r[0] == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
			char h[3] = { r[1], r[2], 0 };

			*w++ = (char)strtol(h, NULL, 16);
			r += 3;
		} else {
			*w++ = *r++;
		}
	}
	*w = '\0';
}

bool itchdl_key_id(const char *signed_url, char *out, size_t n)
{
	char last[1024], json[768];
	const char *q = strchr(signed_url, '?'), *s, *e;
	size_t o = 0;
	unsigned acc = 0;
	int bits = 0;
	char *p;

	out[0] = '\0';
	e = q ? q : signed_url + strlen(signed_url);
	while (e > signed_url && e[-1] == '/') e--;
	for (s = e; s > signed_url && s[-1] != '/'; s--) ;
	if ((size_t)(e - s) >= sizeof last) return false;
	memcpy(last, s, (size_t)(e - s));
	last[e - s] = '\0';
	unpercent(last);
	if ((p = strchr(last, '.'))) *p = '\0';
	for (p = last; *p && o + 1 < sizeof json; p++) {
		int v = b64v((unsigned char)*p);

		if (v < 0) continue;
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) { bits -= 8; json[o++] = (char)((acc >> bits) & 0xFF); }
	}
	json[o] = '\0';
	{
		jsv root = js_root(json, o), id;
		long v;

		if (!js_member(root, "id", &id) || (v = js_int(id)) <= 0) return false;
		snprintf(out, n, "%ld", v);
	}
	return true;
}

/* ---- choosing a file ---------------------------------------------------------- */

static const char *real_ext(const char *name);
static const char *ext_of(const char *name)
{
	const char *d = strrchr(name, '.');

	return d && d[1] ? d + 1 : "";
}

/* Where `ext` comes in a comma list, or -1 */
static int ext_rank(const char *ext, const char *exts)
{
	const char *p = exts;
	int i = 0;

	while (*p) {
		size_t l = strcspn(p, ",");

		if (l == strlen(ext) && !strncasecmp(p, ext, l)) return i;
		p += l;
		if (*p == ',') p++;
		i++;
	}
	return -1;
}

/* A .png is a game only as a PICO-8 cart, "x.p8.png" */
static bool png_not_cart(const char *name, const char *e)
{
	size_t l = strlen(name);

	return !strcasecmp(e, "png") && (l < 7 || strcasecmp(name + l - 7, ".p8.png"));
}

bool itchdl_ext_in(const char *name, const char *exts, bool zip)
{
	const char *e = real_ext(name);

	if (!zip && !strcasecmp(e, "zip")) return false;
	if (png_not_cart(name, e)) return false;
	return ext_rank(e, exts) >= 0;
}

/* Files that are plainly not a game for a handheld */
static const char *const NOT_A_GAME[] = {
	"pdf", "txt", "md", "rtf", "doc", "docx", "html", "htm", "png", "jpg", "jpeg", "gif", "bmp",
	"webp", "svg", "psd", "mp3", "ogg", "wav", "flac", "aac", "m4a", "mp4", "avi", "mkv", "mov",
	"webm", "exe", "msi", "dmg", "pkg", "app", "apk", "ipa", "deb", "rpm", "appimage", "x86_64",
	"love", "swf", "tar", "gz", "tgz", "rar", "7z", "bz2", "xz", "epub", "mobi", "ttf", "otf",
	"pocket", "sav", "srm", "ips", "bps", "ups", "json", "xml", "csv", "sh", "bat", "py", "lua",
	"p8", "nds", "3ds", "cia", "nsp", "iso", "cue", "bin", "chd", "img",
	NULL
};
/* Other machines' ROMs: left out when they aren't this console's */
static const char *const ROM_EXTS[] = {
	"nes", "fds", "unf", "unif", "sfc", "smc", "gb", "gbc", "cgb", "dmg", "gba", "agb", "md", "gen",
	"smd", "sms", "gg", "pce", "sgx", "ngp", "ngc", "ngpc", "npc", "32x", "vb", "ws", "wsc", "a26",
	"a78", "lnx", "col", "sg", "int", NULL
};
/* Labels that say a file is for something else */
static const char *const ELSEWHERE[] = {
	"web", "html", "browser", "source", "src", "windows", "win", "mac", "macos", "osx", "linux",
	"android", "pocket", "analogue", "soundtrack", "ost", "manual", "artbook", "wallpaper",
	"press", "cartridge", "label", "box", "print", "printable", "map", "poster", "guide", "steam",
	"switch", "mega65", "demo", NULL
};

static bool in_list(const char *w, const char *const *l)
{
	int i;

	for (i = 0; l[i]; i++) if (!strcasecmp(w, l[i])) return true;
	return false;
}

/* Whether the label has one of `l`'s words in it, as a word */
static bool has_word(const char *name, const char *const *l)
{
	char w[64];
	const char *p = name;

	while (*p) {
		size_t k = 0;

		while (*p && !isalnum((unsigned char)*p)) p++;
		while (*p && isalnum((unsigned char)*p)) { if (k + 1 < sizeof w) w[k++] = *p; p++; }
		w[k] = '\0';
		if (k && in_list(w, l)) return true;
	}
	return false;
}

/* An extension, when the name looks like a file's (letters and digits, at
 * most five, after a dot); "" for a label */
static const char *real_ext(const char *name)
{
	const char *e = ext_of(name);
	size_t i, l = strlen(e);

	if (l == 0 || l > 8) return "";
	for (i = 0; i < l; i++) if (!isalnum((unsigned char)e[i])) return "";
	if (strspn(e, "0123456789") == l) return "";   /* "v1.2" */
	return e;
}

int itchdl_rank(const itchdl_upload *u, int n, const char *exts, int *order, bool *several)
{
	int score[ITCHDL_UPLOADS], k = 0, i, j;

	for (i = 0; i < n && i < ITCHDL_UPLOADS; i++) {
		const char *e = real_ext(u[i].name);
		int r = e[0] && !png_not_cart(u[i].name, e) ? ext_rank(e, exts) : -1, sc;

		if (e[0] && !strcasecmp(e, "zip")) sc = 100;
		/* as the pak chooses (its roms.ScoreUpload): a Game Boy game's
		 * Color version over its plain one, and a PICO-8 cart as a picture
		 * (.p8.png, its label on it) over the text one */
		else if (r >= 0 && !strcasecmp(e, "gbc") && ext_rank("gb", exts) >= 0) sc = -2;
		else if (r >= 0 && !strcasecmp(e, "png") && ext_rank("p8", exts) >= 0) sc = -2;
		else if (r >= 0) sc = r;
		else if (!e[0]) sc = 200;
		else if (in_list(e, NOT_A_GAME) || in_list(e, ROM_EXTS)) continue;
		else sc = 200;      /* something unheard of: its bytes will say */
		if (has_word(u[i].name, ELSEWHERE)) sc += 500;
		score[k] = sc;
		order[k++] = i;
	}
	/* best first, the page's order kept between equals */
	for (i = 1; i < k; i++)
		for (j = i; j > 0 && score[j] < score[j - 1]; j--) {
			int t = score[j]; score[j] = score[j - 1]; score[j - 1] = t;
			t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
		}
	if (several) *several = k > 1 && score[1] == score[0];
	return k;
}

bool itchdl_sniff(const char *path, char *folder, size_t fn, char *ext, size_t en)
{
	static const unsigned char gb_logo[8] = { 0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B };
	static const unsigned char gba_logo[8] = { 0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21 };
	static unsigned char h[0x8000];
	static const long tmr[] = { 0x7FF0, 0x3FF0, 0x1FF0 };
	FILE *f = fopen(path, "rb");
	size_t got;
	const char *fo = NULL, *ex = NULL;
	int i;

	if (!f) return false;
	got = fread(h, 1, sizeof h, f);
	fclose(f);
	if (got >= 4 && !memcmp(h, "PK\x03\x04", 4)) { fo = ""; ex = "zip"; }
	/* PICO-8, as the pak tells it (its roms.DetectROMExt): a text cart
	 * starts "pico-8 cartridge"; a picture cart is a PNG 128 wide */
	else if (got >= 16 && !memcmp(h, "pico-8 cartridge", 16)) { fo = "PICO-8"; ex = "p8"; }
	else if (got >= 24 && !memcmp(h, "\x89PNG", 4) && h[16] == 0 && h[17] == 0 && h[18] == 0 && h[19] == 128) {
		fo = "PICO-8";
		ex = "p8.png";
	}
	else if (got >= 4 && !memcmp(h, "NES\x1A", 4)) { fo = "NES"; ex = "nes"; }
	else if (got >= 12 && !memcmp(h + 4, gba_logo, 8)) { fo = "Game Boy Advance"; ex = "gba"; }
	else if (got >= 0x150 && !memcmp(h + 0x104, gb_logo, 8)) {
		bool cgb = (h[0x143] & 0x80) != 0;

		fo = cgb ? "Game Boy Color" : "Game Boy";
		ex = cgb ? "gbc" : "gb";
	} else if (got >= 0x108 && !memcmp(h + 0x100, "SEGA", 4)) { fo = "Genesis"; ex = "md"; }
	else
		for (i = 0; i < 3; i++)
			if ((size_t)tmr[i] + 16 <= got && !memcmp(h + tmr[i], "TMR SEGA", 8)) {
				int region = h[tmr[i] + 15] >> 4;

				fo = region >= 5 && region <= 7 ? "Game Gear" : "Master System";
				ex = region >= 5 && region <= 7 ? "gg" : "sms";
				break;
			}
	if (!fo) return false;
	snprintf(folder, fn, "%s", fo);
	snprintf(ext, en, "%s", ex);
	return true;
}

/* ---- the web flow ------------------------------------------------------------- */

static int web_uploads(itchdl *d, itchdl_upload *out, int max, bool *paid, char *why, size_t wn)
{
	char csrf[160], url[400], signed_url[1024];
	net_field f[2];
	char *b;
	int http, n;

	snprintf(d->jar, sizeof d->jar, "/tmp/chaos-itch-%ld.jar", (long)getpid());
	unlink(d->jar);
	/* 1: the game's page, for its token (and the jar's first cookie) */
	if (!(b = ask(d->link, NULL, d->jar, NULL, 0, true, &http)) || http != 200) {
		d->http = b ? http : 0;
		say_http(b ? http : 0, why, wn);
		free(b);
		return -1;
	}
	if (!itchdl_csrf(b, csrf, sizeof csrf)) {
		free(b);
		snprintf(why, wn, "itch.io's page has changed");
		fprintf(stderr, "itchdl: no csrf_token on the game's page\n");
		return -1;
	}
	free(b);
	d->http = 200;
	/* 2: what the Download button asks for, with nothing paid */
	snprintf(url, sizeof url, "%s/download_url", d->link);
	f[0] = (net_field){ "csrf_token", csrf };
	f[1] = (net_field){ "suggested_amount", "0" };
	if (!(b = ask(url, NULL, d->jar, f, 2, false, &http))) { say_http(0, why, wn); return -1; }
	{
		jsv root = js_root(b, strlen(b)), u;

		signed_url[0] = '\0';
		if (js_member(root, "url", &u)) js_str(u, signed_url, sizeof signed_url);
	}
	if (!signed_url[0]) {
		/* A price to pay: the web flow has nothing to give without one */
		if (!reply_error(b, why, wn)) snprintf(why, wn, "A paid game");
		*paid = true;
		free(b);
		fprintf(stderr, "itchdl: no download without paying (HTTP %d)\n", http);
		return -1;
	}
	free(b);
	if (strncmp(signed_url, "https://", 8) || !itchdl_key_id(signed_url, d->key_id, sizeof d->key_id)) {
		snprintf(why, wn, "itch.io's page has changed");
		fprintf(stderr, "itchdl: no download key in the signed address\n");
		return -1;
	}
	/* 3: the signed page, its files and its token */
	if (!(b = ask(signed_url, NULL, d->jar, NULL, 0, true, &http)) || http != 200) {
		say_http(b ? http : 0, why, wn);
		free(b);
		return -1;
	}
	itchdl_csrf(b, d->csrf, sizeof d->csrf);
	n = itchdl_parse_page(b, out, max);
	free(b);
	fprintf(stderr, "itchdl: %d file%s on the download page\n", n, n == 1 ? "" : "s");
	return n;
}

static bool web_file(itchdl *d, const itchdl_upload *u, char *url, size_t n, char *why, size_t wn)
{
	char post[400];
	net_field f[2];
	char *b;
	int http;

	snprintf(post, sizeof post, "%s/file/%s", d->link, u->id);
	f[0] = (net_field){ "csrf_token", d->csrf };
	f[1] = (net_field){ "download_key_id", d->key_id };
	if (!(b = ask(post, NULL, d->jar, f, 2, false, &http))) { say_http(0, why, wn); return false; }
	url[0] = '\0';
	{
		jsv root = js_root(b, strlen(b)), v;

		if (js_member(root, "url", &v)) js_str(v, url, n);
	}
	if (!url[0] || strncmp(url, "https://", 8)) {
		if (!reply_error(b, why, wn)) say_http(http == 200 ? 500 : http, why, wn);
		free(b);
		url[0] = '\0';
		return false;
	}
	free(b);
	return true;
}

/* ---- the API ------------------------------------------------------------------- */

static int api_uploads(itchdl *d, itchdl_upload *out, int max, bool *paid, char *why, size_t wn)
{
	char url[400];
	char *b;
	int http, n = 0;
	jsv root, v, it = { 0 }, e;

	/* the game's id */
	snprintf(url, sizeof url, "%s/data.json", d->link);
	if (!(b = ask(url, NULL, NULL, NULL, 0, false, &http)) || http != 200) {
		d->http = b ? http : 0;
		say_http(b ? http : 0, why, wn);
		free(b);
		return -1;
	}
	d->http = 200;
	root = js_root(b, strlen(b));
	if (!js_member(root, "id", &v) || js_int(v) <= 0) {
		free(b);
		snprintf(why, wn, "itch.io's page has changed");
		return -1;
	}
	snprintf(d->game_id, sizeof d->game_id, "%ld", js_int(v));
	free(b);
	/* the purchase, when there is one */
	d->key_id[0] = '\0';
	snprintf(url, sizeof url, ITCHDL_API "/profile/owned-keys?game_ids=%s&page=1", d->game_id);
	if (!(b = ask(url, d->api_key, NULL, NULL, 0, false, &http))) { say_http(0, why, wn); return -1; }
	if (http == 401 || http == 403) {
		free(b);
		snprintf(why, wn, "itch.io didn't take your key");
		return -1;
	}
	root = js_root(b, strlen(b));
	if (http == 200 && js_member(root, "owned_keys", &v) && js_next(v, &it, &e)) {
		jsv id, gid;

		/* owned_keys is an array, or {} when there are none */
		if (js_member(e, "id", &id) && js_int(id) > 0 &&
		    (!js_member(e, "game_id", &gid) || js_int(gid) == atol(d->game_id)))
			snprintf(d->key_id, sizeof d->key_id, "%ld", js_int(id));
	}
	free(b);
	/* the files */
	snprintf(url, sizeof url, ITCHDL_API "/games/%s/uploads%s%s", d->game_id,
	         d->key_id[0] ? "?download_key_id=" : "", d->key_id);
	if (!(b = ask(url, d->api_key, NULL, NULL, 0, false, &http))) { say_http(0, why, wn); return -1; }
	if (http == 401 || http == 403) {
		free(b);
		*paid = true;
		snprintf(why, wn, "Not one you own");
		return -1;
	}
	if (http != 200) {
		if (!reply_error(b, why, wn)) say_http(http, why, wn);
		free(b);
		return -1;
	}
	root = js_root(b, strlen(b));
	if (reply_error(b, why, wn)) {
		if (!d->key_id[0]) *paid = true;
		free(b);
		return -1;
	}
	memset(&it, 0, sizeof it);
	if (js_member(root, "uploads", &v))
		while (n < max && js_next(v, &it, &e)) {
			jsv id, fn, sz;
			itchdl_upload u;

			memset(&u, 0, sizeof u);
			if (!js_member(e, "id", &id) || !js_member(e, "filename", &fn)) continue;
			snprintf(u.id, sizeof u.id, "%ld", js_int(id));
			js_str(fn, u.name, sizeof u.name);
			if (js_member(e, "size", &sz)) u.size = js_int(sz);
			if (u.name[0] && js_int(id) > 0) out[n++] = u;
		}
	free(b);
	/* A paid game not bought lists nothing, rather than refusing */
	fprintf(stderr, "itchdl: %d file%s through the API (%s)\n", n, n == 1 ? "" : "s",
	        d->key_id[0] ? "bought" : "no purchase");
	return n;
}

static bool api_file(itchdl *d, const itchdl_upload *u, char *url, size_t n, char *why, size_t wn)
{
	char ask_url[400];
	net_req r;
	int http;

	snprintf(ask_url, sizeof ask_url, ITCHDL_API "/uploads/%s/download%s%s", u->id,
	         d->key_id[0] ? "?download_key_id=" : "", d->key_id);
	memset(&r, 0, sizeof r);
	r.url = ask_url;
	r.bearer = d->api_key;
	r.timeout_s = 30;
	http = net_request_redirect(&r, url, n);
	if (http >= 300 && http < 400 && !strncmp(url, "https://", 8)) return true;
	url[0] = '\0';
	if (http == 200) {
		/* Some answer with the address in JSON instead */
		char *b = ask(ask_url, d->api_key, NULL, NULL, 0, false, &http);

		if (b) {
			jsv root = js_root(b, strlen(b)), v;

			if (js_member(root, "url", &v)) js_str(v, url, n);
			free(b);
			if (!strncmp(url, "https://", 8)) return true;
		}
		url[0] = '\0';
	}
	if (http == 401 || http == 403) snprintf(why, wn, "Not one you own");
	else say_http(http < 0 ? 0 : http, why, wn);
	return false;
}

/* ---- either ------------------------------------------------------------------ */

int itchdl_uploads(itchdl *d, itchdl_upload *out, int max, bool *paid, char *why, size_t wn)
{
	bool p = false;
	int n;

	why[0] = '\0';
	if (max > ITCHDL_UPLOADS) max = ITCHDL_UPLOADS;
	n = d->api_key && d->api_key[0] ? api_uploads(d, out, max, &p, why, wn)
	                                : web_uploads(d, out, max, &p, why, wn);
	if (paid) *paid = p;
	return n;
}

bool itchdl_file_url(itchdl *d, const itchdl_upload *u, char *url, size_t n, char *why, size_t wn)
{
	why[0] = '\0';
	return d->api_key && d->api_key[0] ? api_file(d, u, url, n, why, wn)
	                                   : web_file(d, u, url, n, why, wn);
}

int itchdl_owned(const char *key, char (*links)[192], int max)
{
	int page, n = 0;

	for (page = 1; page <= 40 && n < max; page++) {
		char url[160];
		char *b;
		int http, got = 0;
		jsv root, v, it = { 0 }, e;

		snprintf(url, sizeof url, ITCHDL_API "/profile/owned-keys?page=%d", page);
		if (!(b = ask(url, key, NULL, NULL, 0, false, &http))) return -1;
		if (http != 200) { free(b); return -1; }
		root = js_root(b, strlen(b));
		/* owned_keys is an array, or {} past the last page */
		if (js_member(root, "owned_keys", &v))
			while (n < max && js_next(v, &it, &e)) {
				jsv g, u;

				got++;
				if (js_member(e, "game", &g) && js_member(g, "url", &u) &&
				    js_str(u, links[n], sizeof links[0]) && !strncmp(links[n], "https://", 8))
					n++;
			}
		free(b);
		if (!got) break;
	}
	fprintf(stderr, "itchdl: %d game%s owned\n", n, n == 1 ? "" : "s");
	return n;
}

void itchdl_done(itchdl *d)
{
	if (d->jar[0]) unlink(d->jar);
	memset(d->csrf, 0, sizeof d->csrf);
	d->jar[0] = '\0';
}
