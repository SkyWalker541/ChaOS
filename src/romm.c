/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: RomM - see romm.h */
#include "romm.h"
#include "net.h"
#include "rajson.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROMM_REPLY (1 << 20)   /* a page of games, with their files, fits */
#define CLIENT "ChaOS"

static void say(char *why, size_t wn, const char *s) { if (why && wn) snprintf(why, wn, "%s", s); }

/* A reply's status, as a person would read it */
static void http_why(int http, char *why, size_t wn)
{
	if (http == 0) say(why, wn, "Couldn't reach the server");
	else if (http == 401 || http == 403) say(why, wn, "The server said no: sign in again");
	else if (http == 404) say(why, wn, "The server doesn't have that");
	else {
		char m[64];

		snprintf(m, sizeof m, "The server answered %d", http);
		say(why, wn, m);
	}
}

/* ---- the connection, on the card ------------------------------------------ */

static bool read_line(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "r");

	out[0] = '\0';
	if (!f) return false;
	if (!fgets(out, (int)n, f)) out[0] = '\0';
	fclose(f);
	out[strcspn(out, "\r\n")] = '\0';
	return out[0] != '\0';
}

static bool write_line(const char *path, const char *s, int mode)
{
	char tmp[600];
	int fd;
	size_t l = strlen(s);
	bool ok;

	snprintf(tmp, sizeof tmp, "%s.new", path);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0) return false;
	ok = write(fd, s, l) == (ssize_t)l && write(fd, "\n", 1) == 1;
	close(fd);
	return ok && rename(tmp, path) == 0;
}

bool romm_load(romm_conn *c, const char *dir)
{
	char p[512];

	memset(c, 0, sizeof *c);
	snprintf(p, sizeof p, "%s/server", dir);  read_line(p, c->url, sizeof c->url);
	snprintf(p, sizeof p, "%s/token", dir);   read_line(p, c->token, sizeof c->token);
	snprintf(p, sizeof p, "%s/device", dir);  read_line(p, c->device_id, sizeof c->device_id);
	snprintf(p, sizeof p, "%s/user", dir);    read_line(p, c->user, sizeof c->user);
	return c->url[0] && c->token[0];
}

bool romm_save(const romm_conn *c, const char *dir)
{
	char p[512];
	bool ok = true;

	mkdir(dir, 0700);
	snprintf(p, sizeof p, "%s/server", dir); ok &= write_line(p, c->url, 0644);
	snprintf(p, sizeof p, "%s/token", dir);
	if (c->token[0]) ok &= write_line(p, c->token, 0600); else unlink(p);
	snprintf(p, sizeof p, "%s/device", dir);
	if (c->device_id[0]) ok &= write_line(p, c->device_id, 0644); else unlink(p);
	snprintf(p, sizeof p, "%s/user", dir);
	if (c->user[0]) ok &= write_line(p, c->user, 0644); else unlink(p);
	return ok;
}

void romm_forget(romm_conn *c, const char *dir)
{
	c->token[0] = c->device_id[0] = c->user[0] = '\0';
	romm_save(c, dir);
}

bool romm_set_url(romm_conn *c, const char *typed)
{
	char u[ROMM_STR];
	const char *p = typed;
	size_t l;

	while (*p == ' ') p++;
	if (!*p) return false;
	if (strncasecmp(p, "http://", 7) && strncasecmp(p, "https://", 8))
		snprintf(u, sizeof u, "http://%s", p);
	else
		snprintf(u, sizeof u, "%s", p);
	l = strlen(u);
	while (l > 0 && (u[l - 1] == '/' || u[l - 1] == ' ')) u[--l] = '\0';
	if (strchr(u, ' ') || strlen(u) < 10) return false;
	if (strcmp(c->url, u)) {
		/* another server: none of this one's sign-in is any good there */
		c->token[0] = c->device_id[0] = c->user[0] = '\0';
	}
	snprintf(c->url, sizeof c->url, "%s", u);
	return true;
}

/* ---- requests ------------------------------------------------------------- */

static char *reply_buf(void) { return malloc(ROMM_REPLY); }

static long call(const romm_conn *c, const char *method, const char *path, const char *json,
                 char *out, size_t outn, int *http, int timeout)
{
	char url[1024];
	net_req r;

	memset(&r, 0, sizeof r);
	snprintf(url, sizeof url, "%s%s", c->url, path);
	r.method = method;
	r.url = url;
	r.bearer = c->token[0] ? c->token : NULL;
	r.json = json;
	r.timeout_s = timeout;
	return net_request(&r, out, outn, http);
}

/* A string as JSON wants it, quotes and all */
static void jesc(char *out, size_t n, const char *s)
{
	size_t o = 0;

	if (n < 3) { if (n) out[0] = '\0'; return; }
	out[o++] = '"';
	for (; *s && o + 7 < n; s++) {
		unsigned char ch = (unsigned char)*s;

		if (ch == '"' || ch == '\\') { out[o++] = '\\'; out[o++] = (char)ch; }
		else if (ch < 0x20) o += (size_t)snprintf(out + o, n - o, "\\u%04x", ch);
		else out[o++] = (char)ch;
	}
	out[o++] = '"';
	out[o] = '\0';
}

static void jstr(jsv v, const char *k, char *out, size_t n)
{
	jsv x;

	out[0] = '\0';
	if (js_member(v, k, &x)) js_str(x, out, n);
}

static long jint(jsv v, const char *k)
{
	jsv x;

	return js_member(v, k, &x) ? js_int(x) : 0;
}

/* ---- signing in ----------------------------------------------------------- */

int romm_pair_begin(romm_conn *c, const char *device_name, romm_pair *p, char *why, size_t wn)
{
	static const char *const scopes =
		"[\"me.read\",\"platforms.read\",\"roms.read\",\"firmware.read\",\"assets.read\","
		"\"assets.write\",\"devices.read\",\"devices.write\"]";
	char body[1024], name[200], *buf = reply_buf(), path[ROMM_STR];
	int http = 0;
	long n;
	jsv root;

	memset(p, 0, sizeof *p);
	if (!buf) return -1;
	jesc(name, sizeof name, device_name);
	if (c->device_id[0]) {
		char id[120];

		jesc(id, sizeof id, c->device_id);
		snprintf(body, sizeof body,
		         "{\"client_device_identifier\":%s,\"name\":%s,\"client\":\"" CLIENT "\","
		         "\"platform\":\"TrimUI Brick\",\"requested_scopes\":%s}", id, name, scopes);
	} else {
		snprintf(body, sizeof body,
		         "{\"name\":%s,\"client\":\"" CLIENT "\",\"platform\":\"TrimUI Brick\","
		         "\"requested_scopes\":%s}", name, scopes);
	}
	n = call(c, "POST", "/api/auth/device/init", body, buf, ROMM_REPLY, &http, 20);
	if (http == 404 || http == 405) { free(buf); return 0; }
	if (n < 0 || http < 200 || http >= 300) { http_why(http, why, wn); free(buf); return -1; }
	root = js_root(buf, (size_t)n);
	jstr(root, "device_code", p->device_code, sizeof p->device_code);
	jstr(root, "user_code", p->user_code, sizeof p->user_code);
	jstr(root, "verification_path_complete", path, sizeof path);
	if (!path[0]) jstr(root, "verification_path", path, sizeof path);
	snprintf(p->verify, sizeof p->verify, "%s%s", path[0] == '/' ? c->url : "", path);
	p->interval = (int)jint(root, "interval");
	p->expires = (int)jint(root, "expires_in");
	if (p->interval <= 0) p->interval = 5;
	if (p->expires <= 0) p->expires = 600;
	free(buf);
	if (!p->device_code[0] || !p->user_code[0]) {
		say(why, wn, "The server's pairing answer made no sense");
		return -1;
	}
	return 1;
}

int romm_pair_poll(romm_conn *c, const romm_pair *p, char *why, size_t wn)
{
	char body[400], code[240], *buf = reply_buf(), detail[64];
	int http = 0;
	long n;
	jsv root;

	if (!buf) return ROMM_PAIR_ERROR;
	jesc(code, sizeof code, p->device_code);
	snprintf(body, sizeof body, "{\"device_code\":%s}", code);
	n = call(c, "POST", "/api/auth/device/token", body, buf, ROMM_REPLY, &http, 20);
	if (n < 0 || http == 0) { free(buf); say(why, wn, "Couldn't reach the server"); return ROMM_PAIR_ERROR; }
	root = js_root(buf, (size_t)n);
	if (http >= 200 && http < 300) {
		jstr(root, "access_token", c->token, sizeof c->token);
		jstr(root, "device_id", c->device_id, sizeof c->device_id);
		free(buf);
		if (!c->token[0]) { say(why, wn, "The server sent no token"); return ROMM_PAIR_ERROR; }
		return ROMM_PAIR_OK;
	}
	jstr(root, "detail", detail, sizeof detail);
	free(buf);
	if (!strcmp(detail, "authorization_pending")) return ROMM_PAIR_WAIT;
	if (!strcmp(detail, "slow_down")) return ROMM_PAIR_SLOW;
	if (!strcmp(detail, "access_denied")) { say(why, wn, "Pairing was turned down"); return ROMM_PAIR_DENIED; }
	if (!strcmp(detail, "expired_token")) { say(why, wn, "The code ran out: start again"); return ROMM_PAIR_EXPIRED; }
	http_why(http, why, wn);
	return ROMM_PAIR_ERROR;
}

bool romm_exchange(romm_conn *c, const char *code, char *why, size_t wn)
{
	char body[200], q[120], *buf = reply_buf();
	int http = 0;
	long n;
	romm_conn anon = *c;

	if (!buf) return false;
	anon.token[0] = '\0';
	jesc(q, sizeof q, code);
	snprintf(body, sizeof body, "{\"code\":%s}", q);
	n = call(&anon, "POST", "/api/client-tokens/exchange", body, buf, ROMM_REPLY, &http, 20);
	if (n < 0 || http < 200 || http >= 300) {
		if (http == 400 || http == 404) say(why, wn, "That code isn't right, or ran out");
		else http_why(http, why, wn);
		free(buf);
		return false;
	}
	jstr(js_root(buf, (size_t)n), "raw_token", c->token, sizeof c->token);
	free(buf);
	if (!c->token[0]) { say(why, wn, "The server sent no token"); return false; }
	return true;
}

bool romm_whoami(romm_conn *c, char *why, size_t wn)
{
	char *buf = reply_buf();
	int http = 0;
	long n;

	if (!buf) return false;
	n = call(c, NULL, "/api/users/me", NULL, buf, ROMM_REPLY, &http, 20);
	if (n < 0 || http < 200 || http >= 300) { http_why(http, why, wn); free(buf); return false; }
	jstr(js_root(buf, (size_t)n), "username", c->user, sizeof c->user);
	free(buf);
	return true;
}

bool romm_register(romm_conn *c, const char *device_name, char *why, size_t wn)
{
	char body[600], name[200], *buf;
	int http = 0;
	long n;
	jsv root;

	if (c->device_id[0]) return true;
	if (!(buf = reply_buf())) return false;
	jesc(name, sizeof name, device_name);
	snprintf(body, sizeof body,
	         "{\"name\":%s,\"platform\":\"TrimUI Brick\",\"client\":\"" CLIENT "\","
	         "\"sync_mode\":\"push_pull\"}", name);
	n = call(c, "POST", "/api/devices", body, buf, ROMM_REPLY, &http, 20);
	if (n < 0 || http < 200 || http >= 300) { http_why(http, why, wn); free(buf); return false; }
	root = js_root(buf, (size_t)n);
	jstr(root, "device_id", c->device_id, sizeof c->device_id);
	if (!c->device_id[0]) jstr(root, "id", c->device_id, sizeof c->device_id);
	free(buf);
	if (!c->device_id[0]) { say(why, wn, "The server gave this Brick no id"); return false; }
	return true;
}

/* ---- the library ---------------------------------------------------------- */

/* RomM's platform slugs (IGDB's) and fs_slugs (its folder names), each to
 * the ChaOS system it is: from RomM's own list as Grout maps it */
static const struct { const char *slug, *tag; } g_map[] = {
	{ "nes", "NES" }, { "famicom", "NES" }, { "fds", "NES" },
	{ "sms", "SMS" }, { "master-system", "SMS" },
	{ "gb", "GB" }, { "gbc", "GBC" }, { "gba", "GBA" },
	{ "genesis", "MD" }, { "genesis-slash-megadrive", "MD" }, { "megadrive", "MD" },
	{ "tg16", "PCE" }, { "turbografx-16-slash-pc-engine", "PCE" }, { "pce", "PCE" },
	{ "pc-engine", "PCE" }, { "turbografx-cd", "PCE" }, { "pcecd", "PCE" },
	{ "turbografx-16-slash-pc-engine-cd", "PCE" }, { "supergrafx", "PCE" },
	{ "gamegear", "GG" }, { "gg", "GG" },
	{ "snes", "SFC" }, { "sfam", "SFC" }, { "super-famicom", "SFC" },
	{ "neo-geo-pocket", "NGP" }, { "ngp", "NGP" },
	{ "neo-geo-pocket-color", "NGPC" }, { "ngpc", "NGPC" },
};

const char *romm_tag_for(const char *slug, const char *fs_slug)
{
	size_t i;

	for (i = 0; i < sizeof g_map / sizeof g_map[0]; i++)
		if ((slug && !strcasecmp(slug, g_map[i].slug)) || (fs_slug && !strcasecmp(fs_slug, g_map[i].slug)))
			return g_map[i].tag;
	return NULL;
}

int romm_platforms(const romm_conn *c, romm_platform *out, int max, char *why, size_t wn)
{
	char *buf = reply_buf();
	int http = 0, k = 0;
	long n;
	jsv root, it = { 0 }, e;

	if (!buf) return -1;
	n = call(c, NULL, "/api/platforms", NULL, buf, ROMM_REPLY, &http, 30);
	if (n < 0 || http < 200 || http >= 300) { http_why(http, why, wn); free(buf); return -1; }
	root = js_root(buf, (size_t)n);
	while (js_next(root, &it, &e) && k < max) {
		romm_platform *p = &out[k];
		char custom[96];

		memset(p, 0, sizeof *p);
		p->id = (int)jint(e, "id");
		jstr(e, "slug", p->slug, sizeof p->slug);
		jstr(e, "fs_slug", p->fs_slug, sizeof p->fs_slug);
		jstr(e, "name", p->name, sizeof p->name);
		jstr(e, "custom_name", custom, sizeof custom);
		if (custom[0]) snprintf(p->name, sizeof p->name, "%s", custom);
		p->roms = (int)jint(e, "rom_count");
		p->firmware = (int)jint(e, "firmware_count");
		p->tag = romm_tag_for(p->slug, p->fs_slug);
		if (p->id) k++;
	}
	free(buf);
	return k;
}

static void parse_rom(jsv e, romm_rom *r)
{
	jsv files, x, it = { 0 }, f;

	memset(r, 0, sizeof *r);
	r->id = (int)jint(e, "id");
	r->platform_id = (int)jint(e, "platform_id");
	jstr(e, "name", r->name, sizeof r->name);
	jstr(e, "fs_name", r->fs_name, sizeof r->fs_name);
	if (!r->name[0]) jstr(e, "fs_name_no_ext", r->name, sizeof r->name);
	if (!r->name[0]) snprintf(r->name, sizeof r->name, "%s", r->fs_name);
	r->size = jint(e, "fs_size_bytes");
	r->multi = js_member(e, "has_multiple_files", &x) && js_is_true(x);
	jstr(e, "path_cover_small", r->cover, sizeof r->cover);
	if (!r->cover[0]) jstr(e, "url_cover", r->cover, sizeof r->cover);
	if (!r->multi && js_member(e, "files", &files)) {
		/* the first playable one: RomM can list a manual ahead of the game */
		static const char *const extra[] = { "manual", "walkthrough", "soundtrack", "screenshot",
		                                     "cheat", "patch", "update", "dlc", NULL };
		while (js_next(files, &it, &f)) {
			char cat[32];
			int i;
			bool skip = false;

			jstr(f, "category", cat, sizeof cat);
			for (i = 0; extra[i]; i++) if (!strcasecmp(cat, extra[i])) skip = true;
			if (skip && r->file_id) continue;
			if (!r->file_id || !skip) {
				r->file_id = (int)jint(f, "id");
				jstr(f, "file_name", r->file_name, sizeof r->file_name);
				if (!skip) break;
			}
		}
	}
	if (!r->file_name[0]) snprintf(r->file_name, sizeof r->file_name, "%s", r->fs_name);
}

int romm_roms(const romm_conn *c, int platform_id, int offset, int limit, romm_rom *out,
              int max, int *total, char *why, size_t wn)
{
	char path[200], *buf = reply_buf();
	int http = 0, k = 0;
	long n;
	jsv root, items, it = { 0 }, e;

	*total = 0;
	if (!buf) return -1;
	snprintf(path, sizeof path,
	         "/api/roms?platform_ids=%d&platform_id=%d&offset=%d&limit=%d&order_by=name&order_dir=asc&with_files=true",
	         platform_id, platform_id, offset, limit);
	n = call(c, NULL, path, NULL, buf, ROMM_REPLY, &http, 60);
	if (n < 0 || http < 200 || http >= 300) { http_why(http, why, wn); free(buf); return -1; }
	root = js_root(buf, (size_t)n);
	/* a page ({items, total}), or before RomM 3.5, the list itself */
	if (js_member(root, "items", &items)) *total = (int)jint(root, "total");
	else items = root;
	while (js_next(items, &it, &e) && k < max) {
		parse_rom(e, &out[k]);
		if (out[k].id) k++;
	}
	if (!*total) *total = offset + k;
	free(buf);
	return k;
}

int romm_firmware_list(const romm_conn *c, int platform_id, romm_firmware *out, int max,
                  char *why, size_t wn)
{
	char path[100], *buf = reply_buf();
	int http = 0, k = 0;
	long n;
	jsv root, it = { 0 }, e;

	if (!buf) return -1;
	snprintf(path, sizeof path, "/api/firmware?platform_id=%d", platform_id);
	n = call(c, NULL, path, NULL, buf, ROMM_REPLY, &http, 30);
	if (n < 0 || http < 200 || http >= 300) { http_why(http, why, wn); free(buf); return -1; }
	root = js_root(buf, (size_t)n);
	while (js_next(root, &it, &e) && k < max) {
		romm_firmware *f = &out[k];

		memset(f, 0, sizeof *f);
		f->id = (int)jint(e, "id");
		jstr(e, "file_name", f->file_name, sizeof f->file_name);
		jstr(e, "md5_hash", f->md5, sizeof f->md5);
		f->size = jint(e, "file_size_bytes");
		/* a name with a path in it goes nowhere near Bios/ */
		if (f->id && f->file_name[0] && !strchr(f->file_name, '/') && strcmp(f->file_name, "..")) k++;
	}
	free(buf);
	return k;
}

/* Percent-encoded, as a path segment */
static void seg(char *out, size_t n, const char *s)
{
	size_t o = 0;

	for (; *s && o + 4 < n; s++) {
		unsigned char ch = (unsigned char)*s;

		if (isalnum(ch) || strchr("-._~", ch)) out[o++] = (char)ch;
		else o += (size_t)snprintf(out + o, n - o, "%%%02X", ch);
	}
	out[o] = '\0';
}

void romm_rom_url(const romm_conn *c, const romm_rom *r, char *out, size_t n)
{
	char name[ROMM_STR * 3];

	if (r->multi) {
		seg(name, sizeof name, r->fs_name);
		snprintf(out, n, "%s/api/roms/%d/content/%s", c->url, r->id, name);
	} else {
		seg(name, sizeof name, r->file_name);
		snprintf(out, n, "%s/api/roms/%d/content/%s%s%d", c->url, r->id, name,
		         r->file_id ? "?file_ids=" : "", r->file_id);
		if (!r->file_id) out[strlen(out) - 1] = '\0';   /* no id after all */
	}
}

void romm_cover_url(const romm_conn *c, const romm_rom *r, char *out, size_t n)
{
	if (!r->cover[0]) { out[0] = '\0'; return; }
	if (!strncmp(r->cover, "http://", 7) || !strncmp(r->cover, "https://", 8))
		snprintf(out, n, "%s", r->cover);
	else if (!strncmp(r->cover, "//", 2))
		snprintf(out, n, "https:%s", r->cover);
	else
		snprintf(out, n, "%s%s%s", c->url, r->cover[0] == '/' ? "" : "/", r->cover);
}

void romm_firmware_url(const romm_conn *c, const romm_firmware *f, char *out, size_t n)
{
	char name[600];

	seg(name, sizeof name, f->file_name);
	snprintf(out, n, "%s/api/firmware/%d/content/%s", c->url, f->id, name);
}

/* ---- games.tsv ------------------------------------------------------------ */

bool romm_map_add(const char *dir, int rom_id, const char *folder, const char *file)
{
	char p[512];
	FILE *f;

	if (romm_map_find(dir, folder, file) == rom_id) return true;
	mkdir(dir, 0700);
	snprintf(p, sizeof p, "%s/games.tsv", dir);
	if (!(f = fopen(p, "a"))) return false;
	fprintf(f, "%d\t%s\t%s\n", rom_id, folder, file);
	return fclose(f) == 0;
}

typedef struct { const char *folder, *file; int found; } find_ctx;

static bool find_one(int rom_id, const char *folder, const char *file, void *ctx)
{
	find_ctx *f = ctx;

	if (!strcmp(folder, f->folder) && !strcmp(file, f->file)) f->found = rom_id;
	return true;
}

int romm_map_find(const char *dir, const char *folder, const char *file)
{
	find_ctx f = { folder, file, 0 };

	romm_map_each(dir, find_one, &f);
	return f.found;
}

void romm_map_each(const char *dir, romm_map_fn fn, void *ctx)
{
	char p[512], line[1200];
	FILE *f;

	snprintf(p, sizeof p, "%s/games.tsv", dir);
	if (!(f = fopen(p, "r"))) return;
	while (fgets(line, sizeof line, f)) {
		char *a = strchr(line, '\t'), *b;

		if (!a) continue;
		*a++ = '\0';
		if (!(b = strchr(a, '\t'))) continue;
		*b++ = '\0';
		b[strcspn(b, "\r\n")] = '\0';
		if (atoi(line) > 0 && !fn(atoi(line), a, b, ctx)) break;
	}
	fclose(f);
}

/* ---- saves ---------------------------------------------------------------- */

static void iso(time_t t, char *out, size_t n)
{
	struct tm tm;

	gmtime_r(&t, &tm);
	strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

int romm_negotiate(const romm_conn *c, const romm_local_save *s, int n, int *session,
                   romm_sync_op *ops, int max, char *why, size_t wn)
{
	size_t cap = 512 + (size_t)n * 700, o = 0;
	char *body = malloc(cap), *buf, id[120];
	int http = 0, k = 0, i;
	long got;
	jsv root, arr, it = { 0 }, e;

	*session = 0;
	if (!body) return -1;
	jesc(id, sizeof id, c->device_id);
	o += (size_t)snprintf(body + o, cap - o, "{\"device_id\":%s,\"saves\":[", id);
	for (i = 0; i < n && o + 700 < cap; i++) {
		char name[ROMM_STR + 40], when[32];

		jesc(name, sizeof name, s[i].file_name);
		iso(s[i].mtime, when, sizeof when);
		o += (size_t)snprintf(body + o, cap - o,
		                      "%s{\"rom_id\":%d,\"file_name\":%s,\"content_hash\":\"%s\","
		                      "\"updated_at\":\"%s\",\"file_size_bytes\":%ld}",
		                      i ? "," : "", s[i].rom_id, name, s[i].md5, when, s[i].size);
	}
	snprintf(body + o, cap - o, "]}");
	if (!(buf = reply_buf())) { free(body); return -1; }
	got = call(c, "POST", "/api/sync/negotiate", body, buf, ROMM_REPLY, &http, 60);
	free(body);
	if (got < 0 || http < 200 || http >= 300) {
		if (http == 404) say(why, wn, "Save sync needs RomM 4.9 or newer");
		else http_why(http, why, wn);
		free(buf);
		return -1;
	}
	root = js_root(buf, (size_t)got);
	*session = (int)jint(root, "session_id");
	if (js_member(root, "operations", &arr)) {
		while (js_next(arr, &it, &e) && k < max) {
			romm_sync_op *op = &ops[k];

			memset(op, 0, sizeof *op);
			jstr(e, "action", op->action, sizeof op->action);
			op->rom_id = (int)jint(e, "rom_id");
			op->save_id = (int)jint(e, "save_id");
			jstr(e, "file_name", op->file_name, sizeof op->file_name);
			if (op->action[0] && op->rom_id) k++;
		}
	}
	free(buf);
	return k;
}

bool romm_save_upload(const romm_conn *c, int rom_id, const char *path, char *why, size_t wn)
{
	char url[600], *buf = reply_buf(), dev[200];
	net_req r;
	int http = 0;
	long n;

	if (!buf) return false;
	seg(dev, sizeof dev, c->device_id);
	snprintf(url, sizeof url, "%s/api/saves?rom_id=%d%s%s", c->url, rom_id,
	         c->device_id[0] ? "&device_id=" : "", c->device_id[0] ? dev : "");
	memset(&r, 0, sizeof r);
	r.method = "POST";
	r.url = url;
	r.bearer = c->token;
	r.form_name = "saveFile";
	r.form_file = path;
	r.timeout_s = 120;
	n = net_request(&r, buf, ROMM_REPLY, &http);
	free(buf);
	if (n < 0 || http < 200 || http >= 300) { http_why(http, why, wn); return false; }
	return true;
}

bool romm_save_download(const romm_conn *c, int save_id, const char *path, char *why, size_t wn)
{
	char url[400], part[600], dev[200], *buf;
	net_req r;
	pid_t pid;
	int st = 0, http = 0;

	snprintf(url, sizeof url, "%s/api/saves/%d/content", c->url, save_id);
	snprintf(part, sizeof part, "%s.part", path);
	memset(&r, 0, sizeof r);
	r.url = url;
	r.bearer = c->token;
	r.timeout_s = 120;
	unlink(part);
	pid = net_request_spawn(&r, part);
	if (pid <= 0) { say(why, wn, "Couldn't start the download"); return false; }
	waitpid(pid, &st, 0);
	net_spawn_done(pid);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		unlink(part);
		say(why, wn, "Couldn't download the save");
		return false;
	}
	if (rename(part, path) != 0) { unlink(part); say(why, wn, "Couldn't write the save"); return false; }
	/* told it arrived, so the next sync knows this Brick has it */
	if ((buf = reply_buf())) {
		char p[300];

		seg(dev, sizeof dev, c->device_id);
		snprintf(p, sizeof p, "/api/saves/%d/downloaded%s%s", save_id,
		         c->device_id[0] ? "?device_id=" : "", c->device_id[0] ? dev : "");
		call(c, "POST", p, "{}", buf, ROMM_REPLY, &http, 20);
		free(buf);
	}
	return true;
}

bool romm_sync_complete(const romm_conn *c, int session, int done, int failed)
{
	char path[100], body[120], *buf = reply_buf();
	int http = 0;

	if (!buf || !session) { free(buf); return false; }
	snprintf(path, sizeof path, "/api/sync/sessions/%d/complete", session);
	snprintf(body, sizeof body, "{\"operations_completed\":%d,\"operations_failed\":%d}", done, failed);
	call(c, "POST", path, body, buf, ROMM_REPLY, &http, 20);
	free(buf);
	return http >= 200 && http < 300;
}
