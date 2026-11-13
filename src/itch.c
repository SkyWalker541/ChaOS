/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: box art from itch.io. See itch.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "itch.h"
#include "artscrape.h"
#include "net.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- which feeds ---------------------------------------------------------
 *
 * By ChaOS's folder names (config/systems.cfg). The tags are the ones the
 * pak reads (its internal/itchio/platforms.go), and for the consoles it has
 * none for, the tag itch.io's own browse pages use for that console, counted
 * 2026-11-12: SNES 297 games, Master System 62, Game Gear 10, PC Engine 16.
 * Game Boy Color reads GB Studio's too: a GB Studio game is often a .gbc. */
static const char *const S_GB[]   = { "made-with-gb-studio", "tag-gbstudio", "tag-gameboy-rom", NULL };
static const char *const S_GBC[]  = { "tag-gameboy-color", "tag-gbc", "made-with-gb-studio", NULL };
static const char *const S_GBA[]  = { "tag-gameboy-advance", NULL };
static const char *const S_NES[]  = { "tag-nes-rom", NULL };
static const char *const S_SNES[] = { "tag-snes", NULL };
static const char *const S_MD[]   = { "tag-sega-mega-drive", "tag-genesis-rom", NULL };
static const char *const S_SMS[]  = { "tag-master-system", NULL };
static const char *const S_GG[]   = { "tag-game-gear", NULL };
static const char *const S_PCE[]  = { "tag-pc-engine", NULL };

const char *const *itch_slugs(const char *folder)
{
	static const struct { const char *folder; const char *const *slugs; } map[] = {
		{ "Game Boy", S_GB }, { "Game Boy Color", S_GBC },
		{ "Game Boy Advance", S_GBA }, { "NES", S_NES }, { "SNES", S_SNES },
		{ "Genesis", S_MD }, { "Master System", S_SMS }, { "Game Gear", S_GG },
		{ "TurboGrafx-16", S_PCE },
	};
	size_t i;

	if (!folder) return NULL;
	for (i = 0; i < sizeof map / sizeof map[0]; i++)
		if (!strcmp(map[i].folder, folder)) return map[i].slugs;
	return NULL;
}

/* ---- reading a feed page ------------------------------------------------- */

static void put_utf8(char *out, size_t n, size_t *o, unsigned cp)
{
	unsigned char b[4];
	int k = 0, i;

	if (cp < 0x80) b[k++] = (unsigned char)cp;
	else if (cp < 0x800) { b[k++] = 0xC0 | (cp >> 6); b[k++] = 0x80 | (cp & 0x3F); }
	else if (cp < 0x10000) {
		b[k++] = 0xE0 | (cp >> 12); b[k++] = 0x80 | ((cp >> 6) & 0x3F);
		b[k++] = 0x80 | (cp & 0x3F);
	} else if (cp < 0x110000) {
		b[k++] = 0xF0 | (cp >> 18); b[k++] = 0x80 | ((cp >> 12) & 0x3F);
		b[k++] = 0x80 | ((cp >> 6) & 0x3F); b[k++] = 0x80 | (cp & 0x3F);
	}
	if (*o + (size_t)k >= n) return;
	for (i = 0; i < k; i++) out[(*o)++] = (char)b[i];
}

/* One element's text, `<tag>...</tag>` within [from, to), entities decoded
 * and CDATA unwrapped, tabs and line breaks made spaces (the card's list is
 * a line a game, tab-separated). False when it isn't there. */
static bool element(const char *from, const char *to, const char *tag, char *out, size_t n)
{
	char open[32], close[32];
	const char *s, *e;
	size_t o = 0;

	snprintf(open, sizeof open, "<%s>", tag);
	snprintf(close, sizeof close, "</%s>", tag);
	s = memmem(from, (size_t)(to - from), open, strlen(open));
	if (!s) return false;
	s += strlen(open);
	e = memmem(s, (size_t)(to - s), close, strlen(close));
	if (!e) return false;
	if ((size_t)(e - s) >= 9 && !memcmp(s, "<![CDATA[", 9)) {
		s += 9;
		if (e - s >= 3 && !memcmp(e - 3, "]]>", 3)) e -= 3;
	}
	while (s < e && o + 1 < n) {
		if (*s == '&') {
			const char *semi = memchr(s, ';', (size_t)(e - s));
			size_t len = semi ? (size_t)(semi - s) : 0;

			if (semi && len < 12) {
				unsigned cp = 0;
				bool ok = true;

				if (s[1] == '#') {
					char *end;
					cp = (unsigned)strtoul(s + 2 + (s[2] == 'x'), &end, s[2] == 'x' ? 16 : 10);
					ok = end == semi && cp > 0;
				} else if (len == 4 && !memcmp(s, "&amp", 4)) cp = '&';
				else if (len == 3 && !memcmp(s, "&lt", 3)) cp = '<';
				else if (len == 3 && !memcmp(s, "&gt", 3)) cp = '>';
				else if (len == 5 && !memcmp(s, "&quot", 5)) cp = '"';
				else if (len == 5 && !memcmp(s, "&apos", 5)) cp = '\'';
				else ok = false;
				if (ok) {
					put_utf8(out, n, &o, cp == '\t' || cp == '\n' || cp == '\r' ? ' ' : cp);
					s = semi + 1;
					continue;
				}
			}
		}
		out[o++] = *s == '\t' || *s == '\n' || *s == '\r' ? ' ' : *s;
		s++;
	}
	out[o] = '\0';
	return true;
}

/* An address of itch.io's own: a game's page on <author>.itch.io, or a cover
 * on img.itch.zone. Anything else in a feed is not followed. */
static bool itch_page(const char *u)
{
	const char *host = u + 8, *slash;

	if (strncmp(u, "https://", 8)) return false;
	slash = strchr(host, '/');
	if (!slash || slash - host < 9) return false;
	return !memcmp(slash - 8, ".itch.io", 8) && !strpbrk(u, " \"'<>\\");
}

static bool itch_image(const char *u)
{
	return !strncmp(u, "https://img.itch.zone/", 22) && !strpbrk(u, " \"'<>\\");
}

int itch_feed_parse(const char *xml, size_t len, itch_game *out, int max)
{
	const char *p = xml, *end = xml + len;
	int n = 0;

	if (!xml || !memmem(xml, len < 400 ? len : 400, "<rss", 4)) return -1;
	while (n < max) {
		const char *a = memmem(p, (size_t)(end - p), "<item>", 6), *b;
		itch_game g;

		if (!a) break;
		b = memmem(a, (size_t)(end - a), "</item>", 7);
		if (!b) break;
		memset(&g, 0, sizeof g);
		if ((element(a, b, "plainTitle", g.title, sizeof g.title) ||
		     element(a, b, "title", g.title, sizeof g.title)) && g.title[0] &&
		    element(a, b, "link", g.link, sizeof g.link) && itch_page(g.link) &&
		    element(a, b, "imageurl", g.image, sizeof g.image) && itch_image(g.image))
			out[n++] = g;
		p = b + 7;
	}
	return n;
}

/* ---- matching ------------------------------------------------------------ */

/* Words a title carries for the shop, not as its name: the console in any of
 * its spellings, and what the page says the file is. Dropped wherever they
 * come, as itch.io titles put them first ("NES Homebrew: ...") as often as
 * last. "a real ... game" and "for" go only beside a console word. */
static const char *const SHOP[] = {
	"nes", "famicom", "snes", "gb", "gbc", "gba", "dmg", "gameboy", "gbstudio",
	"genesis", "megadrive", "sms", "gg", "pce", "rom", "roms", "homebrew",
	NULL
};
/* Two-word spellings, joined before the words are looked at */
static const char *const SHOP2[][2] = {
	{ "game", "boy" }, { "mega", "drive" }, { "master", "system" },
	{ "game", "gear" }, { "pc", "engine" }, { "gb", "studio" },
	{ "super", "nintendo" }, { "sega", "genesis" },
};
/* The word after a two-word console spelling that belongs to it */
static bool shop_word(const char *w)
{
	int i;

	for (i = 0; SHOP[i]; i++) if (!strcmp(w, SHOP[i])) return true;
	return false;
}

#define KEY_WORDS 24

static int split(char *s, char **w, int max)
{
	int n = 0;
	char *t = strtok(s, " ");

	while (t && n < max) { w[n++] = t; t = strtok(NULL, " "); }
	return n;
}

void itch_title_key(const char *title, char *out, size_t n)
{
	char norm[256], *w[KEY_WORDS];
	bool drop[KEY_WORDS] = { false };
	int k, i, j;
	size_t o = 0;

	art_norm_loose(title, norm, sizeof norm);
	k = split(norm, w, KEY_WORDS);
	/* Two-word consoles first, with the "color"/"advance" that follows game
	 * boy, then the single words */
	for (i = 0; i + 1 < k; i++)
		for (j = 0; j < (int)(sizeof SHOP2 / sizeof SHOP2[0]); j++)
			if (!strcmp(w[i], SHOP2[j][0]) && !strcmp(w[i + 1], SHOP2[j][1])) {
				drop[i] = drop[i + 1] = true;
				if (i + 2 < k && (!strcmp(w[i + 2], "color") || !strcmp(w[i + 2], "advance")))
					drop[i + 2] = true;
			}
	for (i = 0; i < k; i++) if (shop_word(w[i])) drop[i] = true;
	/* "for", "a real", "game" and "edition" only beside a console word:
	 * "for NES", "a real NES game", "Game Boy edition" */
	for (i = 0; i < k; i++) {
		bool next = i + 1 < k && drop[i + 1], prev = i > 0 && drop[i - 1];

		if (drop[i]) continue;
		if ((!strcmp(w[i], "for") || !strcmp(w[i], "real") || !strcmp(w[i], "a")) && next)
			drop[i] = true;
		/* only last: "a real NES game", "Game Boy edition"; but the "game"
		 * of "NES Game Jam" is the name's */
		else if ((!strcmp(w[i], "game") || !strcmp(w[i], "edition") || !strcmp(w[i], "version")) &&
		         prev && i == k - 1)
			drop[i] = true;
	}
	/* "a" before a dropped "real" */
	for (i = 0; i + 1 < k; i++)
		if (!strcmp(w[i], "a") && drop[i + 1] && !strcmp(w[i + 1], "real")) drop[i] = true;
	for (i = 0; i < k; i++) {
		size_t l = strlen(w[i]);

		if (drop[i]) continue;
		if (o && o + 1 < n) out[o++] = ' ';
		if (o + l >= n) break;
		memcpy(out + o, w[i], l);
		o += l;
	}
	/* Nothing but console words ("NES Homebrew"): keep the title as it was */
	if (!o) { art_norm_loose(title, out, n); return; }
	out[o] = '\0';
}

int itch_score(const char *rom_key, const char *title_key)
{
	char a[256], b[256], *wa[KEY_WORDS], *wb[KEY_WORDS];
	bool used[KEY_WORDS] = { false };
	int na, nb, i, j, common = 0;
	size_t la, lb;

	if (!rom_key[0] || !title_key[0]) return 0;
	if (!strcmp(rom_key, title_key)) return 100;
	snprintf(a, sizeof a, "%s", rom_key);
	snprintf(b, sizeof b, "%s", title_key);
	/* Written together or apart ("Böbl", "Bob L"; "Tobu Tobu Girl",
	 * "TobuTobuGirl"): the same letters without the spaces score high */
	{
		char ca[256], cb[256];
		size_t x = 0, y = 0;

		for (i = 0; a[i]; i++) if (a[i] != ' ') ca[x++] = a[i];
		for (i = 0; b[i]; i++) if (b[i] != ' ') cb[y++] = b[i];
		ca[x] = cb[y] = '\0';
		if (x && !strcmp(ca, cb)) return 95;
	}
	na = split(a, wa, KEY_WORDS);
	nb = split(b, wb, KEY_WORDS);
	for (i = 0; i < na; i++)
		for (j = 0; j < nb; j++)
			if (!used[j] && !strcmp(wa[i], wb[j])) { used[j] = true; common++; break; }
	if (!common) return 0;
	/* Shared words over all the words either has (Dice), so a long title
	 * with one word in common scores low */
	la = (size_t)na; lb = (size_t)nb;
	{
		int s = (int)(200 * common / (int)(la + lb));
		/* The ROM's name wholly inside the title ("Deadeus" in "Deadeus:
		 * The Game Boy Horror") or the other way round is a likely match
		 * even with the extra words */
		if (common == na || common == nb) s = s < 70 ? 70 : s;
		return s > 94 ? 94 : s;
	}
}

int itch_match(const char *rom_name, const itch_game *games, int n, int *best, int max)
{
	char key[256], tk[256];
	int scores[64], got = 0, i, j;

	if (max > 64) max = 64;
	itch_title_key(rom_name, key, sizeof key);
	for (i = 0; i < n; i++) {
		int s;

		itch_title_key(games[i].title, tk, sizeof tk);
		s = itch_score(key, tk);
		if (s < ITCH_SCORE_MIN) continue;
		/* The same page twice (a game under two tags): once */
		for (j = 0; j < got; j++) if (!strcmp(games[best[j]].link, games[i].link)) break;
		if (j < got) continue;
		/* In order, best first; a full list drops its worst */
		if (got < max) got++;
		else if (s <= scores[got - 1]) continue;
		for (j = got - 1; j > 0 && scores[j - 1] < s; j--) {
			scores[j] = scores[j - 1];
			best[j] = best[j - 1];
		}
		scores[j] = s;
		best[j] = i;
	}
	return got;
}

/* ---- the list on the card ------------------------------------------------- */

#define INDEX_MAGIC "#chaos-itch 1"

bool itch_index_save(const char *path, const itch_game *games, int n, time_t when,
                     bool whole)
{
	char tmp[512];
	FILE *f;
	int i;

	snprintf(tmp, sizeof tmp, "%s.part", path);
	if (!(f = fopen(tmp, "w"))) return false;
	fprintf(f, "%s %lld %s\n", INDEX_MAGIC, (long long)when, whole ? "whole" : "part");
	for (i = 0; i < n; i++)
		fprintf(f, "%s\t%s\t%s\n", games[i].title, games[i].link, games[i].image);
	if (fflush(f) != 0 || fsync(fileno(f)) != 0) { fclose(f); unlink(tmp); return false; }
	fclose(f);
	return rename(tmp, path) == 0;
}

itch_game *itch_index_load(const char *path, int *count, time_t *when, bool *whole)
{
	char line[640], state[16] = "";
	long long t = 0;
	itch_game *g = NULL;
	int n = 0, cap = 0;
	FILE *f;

	*count = 0;
	if (when) *when = 0;
	if (whole) *whole = false;
	if (!(f = fopen(path, "r"))) return NULL;
	if (!fgets(line, sizeof line, f) || strncmp(line, INDEX_MAGIC " ", sizeof INDEX_MAGIC) ||
	    sscanf(line + sizeof INDEX_MAGIC, "%lld %15s", &t, state) != 2) {
		fclose(f);
		return NULL;
	}
	while (fgets(line, sizeof line, f)) {
		char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL, *nl;
		itch_game *ng;

		if (!t2) continue;
		*t1 = *t2 = '\0';
		if ((nl = strchr(t2 + 1, '\n'))) *nl = '\0';
		if (!itch_page(t1 + 1) || !itch_image(t2 + 1)) continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 256;
			if (!(ng = realloc(g, (size_t)cap * sizeof *g))) break;
			g = ng;
		}
		snprintf(g[n].title, sizeof g[n].title, "%s", line);
		snprintf(g[n].link, sizeof g[n].link, "%s", t1 + 1);
		snprintf(g[n].image, sizeof g[n].image, "%s", t2 + 1);
		n++;
	}
	fclose(f);
	*count = n;
	if (when) *when = (time_t)t;
	if (whole) *whole = !strcmp(state, "whole");
	return g;
}

/* ---- reading the feeds ------------------------------------------------------ */

/* Where the feeds are. Only a test build points it elsewhere (a server on the
 * desk serving pages in itch.io's format); never anything on the card. */
#ifndef ITCH_BASE
#define ITCH_BASE "https://itch.io"
#endif

#define FEED_TMP   "/tmp/chaos-itch-feed.xml"
#define PAGES_MAX  120     /* per tag: 4,320 games, twice the largest seen */
#define NEW_PAGES  10      /* newest-first pages read to catch up */
#define PAGE_GAP   400     /* ms between pages */
#define SLOW_WAIT  8000    /* ms after a 429 */
#define SLOW_TRIES 3

static struct {
	bool         on;
	const char  *const *slugs;
	int          slug, page, pages_read;
	bool         newest;      /* catching up, not reading the whole list */
	bool         asking;
	int          slow;        /* 429s in a row */
	unsigned     wait_until;
	char         path[512];
	itch_game   *games;
	int          n, cap;
	int          known;       /* games in the list before this run */
	char         problem[96];
} F;

static unsigned now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static bool have_link(const char *link)
{
	int i;

	for (i = 0; i < F.n; i++) if (!strcmp(F.games[i].link, link)) return true;
	return false;
}

static bool add_game(const itch_game *g)
{
	if (F.n == F.cap) {
		int cap = F.cap ? F.cap * 2 : 512;
		itch_game *ng = realloc(F.games, (size_t)cap * sizeof *ng);

		if (!ng) return false;
		F.games = ng;
		F.cap = cap;
	}
	F.games[F.n++] = *g;
	return true;
}

void itch_fetch_cancel(void)
{
	if (F.asking) net_async_abort();
	unlink(FEED_TMP);
	free(F.games);
	memset(&F, 0, sizeof F);
}

bool itch_fetch_begin(const char *folder, const char *index_path)
{
	const char *const *slugs = itch_slugs(folder);
	time_t when;
	bool whole;
	int n;
	itch_game *g;

	itch_fetch_cancel();
	if (!slugs) return false;
	F.slugs = slugs;
	snprintf(F.path, sizeof F.path, "%s", index_path);
	g = itch_index_load(index_path, &n, &when, &whole);
	if (g && whole) {
		F.games = g;
		F.n = F.cap = n;
		F.known = n;
		F.newest = true;
	} else {
		free(g);
	}
	F.slug = 0;
	F.page = 1;
	F.on = true;
	fprintf(stderr, "itch: %s for %s (%d known)\n", F.newest ? "catching up" : "reading the list",
	        folder, F.known);
	return true;
}

static int finish(bool ok)
{
	bool saved = ok && itch_index_save(F.path, F.games, F.n, time(NULL), true);

	fprintf(stderr, "itch: %s, %d games (%d new), %d pages\n",
	        saved ? "list saved" : ok ? "couldn't save the list" : "stopped",
	        F.n, F.n - F.known, F.pages_read);
	if (ok && !saved) snprintf(F.problem, sizeof F.problem, "Couldn't save to the card");
	F.on = false;
	free(F.games);
	F.games = NULL;
	F.n = F.cap = 0;
	return saved ? 0 : -1;
}

/* The page read, or the slug done: the next to ask for */
static void next_slug(void)
{
	F.slug++;
	F.page = 1;
}

int itch_fetch_step(void)
{
	char url[256];

	if (!F.on) return -1;
	if (!F.slugs[F.slug]) return finish(true);
	if (!F.asking) {
		if ((int)(now_ms() - F.wait_until) < 0) return 1;
		snprintf(url, sizeof url, ITCH_BASE "/games/%s%s.xml?page=%d",
		         F.newest ? "newest/" : "", F.slugs[F.slug], F.page);
		if (!net_get_async(url, FEED_TMP, 30)) {
			/* The one request slot is someone else's for now */
			F.wait_until = now_ms() + 500;
			return 1;
		}
		F.asking = true;
		return 1;
	}
	{
		int got = net_async_poll(), http, k, added = 0, i;
		itch_game page[ITCH_PER_PAGE];
		char *xml;
		long len;
		FILE *f;

		if (got == 0) return 1;
		F.asking = false;
		http = net_async_http();
		if (got < 0) {
			unlink(FEED_TMP);
			if (http == 429 && ++F.slow <= SLOW_TRIES) {
				fprintf(stderr, "itch: asked to slow down; waiting\n");
				F.wait_until = now_ms() + SLOW_WAIT * (unsigned)F.slow;
				return 1;
			}
			if (http == 404 && F.page > 1) {   /* past the end */
				next_slug();
				return 1;
			}
			snprintf(F.problem, sizeof F.problem,
			         http == 429 ? "itch.io asked to wait; try later"
			         : http > 0 ? "itch.io didn't answer (%d)" : "Couldn't reach itch.io", http);
			fprintf(stderr, "itch: %s page %d: HTTP %d\n", F.slugs[F.slug], F.page, http);
			return finish(false);
		}
		F.slow = 0;
		F.pages_read++;
		if (!(f = fopen(FEED_TMP, "rb"))) { snprintf(F.problem, sizeof F.problem, "Couldn't read the list"); return finish(false); }
		fseek(f, 0, SEEK_END);
		len = ftell(f);
		fseek(f, 0, SEEK_SET);
		xml = len > 0 && len < (4 << 20) ? malloc((size_t)len + 1) : NULL;
		if (xml && fread(xml, 1, (size_t)len, f) != (size_t)len) { free(xml); xml = NULL; }
		fclose(f);
		unlink(FEED_TMP);
		if (!xml) { snprintf(F.problem, sizeof F.problem, "Couldn't read the list"); return finish(false); }
		xml[len] = '\0';
		k = itch_feed_parse(xml, (size_t)len, page, ITCH_PER_PAGE);
		free(xml);
		if (k < 0) {
			snprintf(F.problem, sizeof F.problem, "itch.io sent something else");
			return finish(false);
		}
		/* Known by page, not by place: catching up stops at the first page
		 * whose games are all in the list already */
		for (i = 0; i < k; i++)
			if (!have_link(page[i].link) && add_game(&page[i])) added++;
		/* The end: a short page, a page all known (itch.io repeats its last
		 * page past the end, and when catching up it means the rest is
		 * known), or the cap */
		if (k < ITCH_PER_PAGE || added == 0 ||
		    F.page >= (F.newest ? NEW_PAGES : PAGES_MAX))
			next_slug();
		else
			F.page++;
		F.wait_until = now_ms() + PAGE_GAP;
		return 1;
	}
}

void itch_fetch_progress(int *games, int *pages)
{
	if (games) *games = F.n;
	if (pages) *pages = F.pages_read;
}

const char *itch_fetch_problem(void)
{
	return F.problem;
}
