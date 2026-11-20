/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: box art from the LaunchBox Games Database. See lbox.h. */
#include "lbox.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LBOX_HOST "https://gamesdb.launchbox-app.com"
#define LBOX_PAGE (1024 * 1024)

/* ---- reading the pages ------------------------------------------------------ */

static void put_utf8(char **o, char *end, unsigned cp)
{
	char b[4];
	int n, i;

	if (cp < 0x80)         { b[0] = (char)cp; n = 1; }
	else if (cp < 0x800)   { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 63)); n = 2; }
	else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 63));
	                         b[2] = (char)(0x80 | (cp & 63)); n = 3; }
	else                   { b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 63));
	                         b[2] = (char)(0x80 | ((cp >> 6) & 63)); b[3] = (char)(0x80 | (cp & 63)); n = 4; }
	for (i = 0; i < n && *o < end; i++) *(*o)++ = b[i];
}

/* `n` characters of HTML text into plain UTF-8: entities decoded, runs of
 * white space made one, trimmed. */
static void unhtml(const char *s, size_t n, char *out, size_t on)
{
	char *o = out, *end = out + on - 1;
	const char *e = s + n;
	bool space = false;

	while (s < e && o < end) {
		if (*s == '&') {
			const char *semi = memchr(s, ';', (size_t)(e - s) < 12 ? (size_t)(e - s) : 12);
			unsigned cp = 0;

			if (semi) {
				if (s[1] == '#' && (s[2] == 'x' || s[2] == 'X')) cp = (unsigned)strtoul(s + 3, NULL, 16);
				else if (s[1] == '#') cp = (unsigned)strtoul(s + 2, NULL, 10);
				else if (!strncmp(s, "&amp;", 5)) cp = '&';
				else if (!strncmp(s, "&lt;", 4)) cp = '<';
				else if (!strncmp(s, "&gt;", 4)) cp = '>';
				else if (!strncmp(s, "&quot;", 6)) cp = '"';
				else if (!strncmp(s, "&apos;", 6)) cp = '\'';
				else if (!strncmp(s, "&nbsp;", 6)) cp = ' ';
			}
			if (cp) {
				if (cp == ' ') space = o > out;
				else {
					if (space && o < end) *o++ = ' ';
					space = false;
					put_utf8(&o, end, cp);
				}
				s = semi + 1;
				continue;
			}
		}
		if (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') {
			space = o > out;
		} else {
			if (space && o < end) *o++ = ' ';
			space = false;
			*o++ = *s;
		}
		s++;
	}
	*o = '\0';
}

/* The text between the end of the tag that starts at or after `p` (its '>')
 * and the next '<'; NULL past `limit`. */
static const char *tag_text(const char *p, const char *limit, const char **end)
{
	const char *gt = strchr(p, '>'), *lt;

	if (!gt || (limit && gt >= limit)) return NULL;
	lt = strchr(gt + 1, '<');
	if (!lt) return NULL;
	*end = lt;
	return gt + 1;
}

int lbox_parse_results(const char *html, lbox_game *out, int max)
{
	const char *p = html;
	int n = 0;

	while (n < max && (p = strstr(p, "href=\"/games/details/"))) {
		const char *next, *h3, *pp, *t, *te;
		lbox_game g;
		int k;

		p += strlen("href=\"/games/details/");
		memset(&g, 0, sizeof g);
		g.id = atoi(p);
		if (g.id <= 0) continue;
		next = strstr(p, "href=\"/games/details/");
		h3 = strstr(p, "<h3");
		if (!h3 || (next && h3 > next)) continue;
		if (!(t = tag_text(h3, next, &te))) continue;
		unhtml(t, (size_t)(te - t), g.title, sizeof g.title);
		pp = strstr(te, "<p");
		if (pp && (!next || pp < next) && (t = tag_text(pp, next, &te)))
			unhtml(t, (size_t)(te - t), g.platform, sizeof g.platform);
		if (!g.title[0]) continue;
		for (k = 0; k < n && out[k].id != g.id; k++) { }
		if (k < n) continue;   /* one card per game */
		out[n++] = g;
	}
	return n;
}

/* An attribute's value inside the tag that `tag` starts, decoded */
static bool attr(const char *tag, const char *name, char *out, size_t on)
{
	const char *gt = strchr(tag, '>'), *a = tag, *v, *q;
	size_t k = strlen(name);

	while ((a = strstr(a, name)) && (!gt || a < gt)) {
		if ((a == tag || a[-1] == ' ' || a[-1] == '\n' || a[-1] == '\t') &&
		    a[k] == '=' && a[k + 1] == '"') {
			v = a + k + 2;
			if (!(q = strchr(v, '"'))) return false;
			unhtml(v, (size_t)(q - v), out, on);
			return true;
		}
		a += k;
	}
	return false;
}

/* What a picture is for a cover: lower is better, -1 not one */
static int kind_rank(const char *kind)
{
	static const char *const ok[] = {
		"Box - Front", "Box - Front - Reconstructed", "Fanart - Box - Front",
		"Box - 3D", "Cart - Front", "Screenshot - Game Title", NULL
	};
	int i;

	for (i = 0; ok[i]; i++) if (!strcasecmp(kind, ok[i])) return i;
	return -1;
}

static int region_rank(const char *region, const char *prefer)
{
	static const char *const order[] = {
		"North America", "World", "United States", "Europe", "United Kingdom", "", NULL
	};
	int i;

	if (prefer && *prefer && !strcasecmp(region, prefer)) return 0;
	for (i = 0; order[i]; i++) if (!strcasecmp(region, order[i])) return i + 1;
	return 20;
}

typedef struct { lbox_image im; int rank; int seq; } ranked;

static int by_rank(const void *a, const void *b)
{
	const ranked *x = a, *y = b;
	return x->rank != y->rank ? x->rank - y->rank : x->seq - y->seq;
}

int lbox_parse_images(const char *html, lbox_image *out, int max, const char *prefer)
{
	ranked *all;
	const char *p = html;
	int n = 0, cap = 256, i;

	if (!(all = calloc((size_t)cap, sizeof *all))) return 0;
	while (n < cap && (p = strstr(p, "href=\"https://images.launchbox-app.com/"))) {
		const char *tag = p, *img, *next;
		char title[LBOX_STR * 2], *dash, *open;
		lbox_image im;
		int kr;

		/* back to the tag's start, for its other attributes */
		while (tag > html && *tag != '<') tag--;
		p += 6;
		memset(&im, 0, sizeof im);
		if (!attr(tag, "href", im.url, sizeof im.url)) continue;
		if (!attr(tag, "data-title", title, sizeof title)) continue;
		/* "<game> - Box - Front Image (North America)": the kind is after the
		 * game's " - ", up to " Image", the region in the brackets */
		if ((open = strrchr(title, '(')) && strchr(open, ')')) {
			char *close = strchr(open, ')');
			*close = '\0';
			snprintf(im.region, sizeof im.region, "%s", open + 1);
			*open = '\0';
		}
		{
			size_t L = strlen(title);
			while (L && title[L - 1] == ' ') title[--L] = '\0';
			if (L > 6 && !strcmp(title + L - 6, " Image")) title[L - 6] = '\0';
		}
		/* the kind is the last of the known kinds the title ends with */
		dash = NULL;
		{
			static const char *const kinds[] = {
				"Box - Front - Reconstructed", "Fanart - Box - Front", "Box - Front",
				"Box - 3D", "Cart - Front", "Screenshot - Game Title", NULL
			};
			int k;
			size_t L = strlen(title);

			for (k = 0; kinds[k]; k++) {
				size_t kl = strlen(kinds[k]);
				if (L >= kl && !strcasecmp(title + L - kl, kinds[k]) &&
				    (L == kl || title[L - kl - 1] == ' ')) { dash = title + L - kl; break; }
			}
		}
		if (!dash) continue;
		snprintf(im.kind, sizeof im.kind, "%s", dash);
		kr = kind_rank(im.kind);
		if (kr < 0) continue;
		next = strstr(p, "href=\"https://images.launchbox-app.com/");
		img = strstr(p, "<img");
		if (img && (!next || img < next)) attr(img, "src", im.thumb, sizeof im.thumb);
		for (i = 0; i < n && strcmp(all[i].im.url, im.url); i++) { }
		if (i < n) continue;
		all[n].im = im;
		all[n].rank = kr * 100 + region_rank(im.region, prefer);
		all[n].seq = n;
		n++;
	}
	qsort(all, (size_t)n, sizeof *all, by_rank);
	if (n > max) n = max;
	for (i = 0; i < n; i++) out[i] = all[i].im;
	free(all);
	return n;
}

const char *lbox_platform(const char *tag)
{
	static const struct { const char *tag, *name; } map[] = {
		{ "gb", "Nintendo Game Boy" }, { "gbc", "Nintendo Game Boy Color" },
		{ "gba", "Nintendo Game Boy Advance" }, { "nes", "Nintendo Entertainment System" },
		{ "fds", "Nintendo Famicom Disk System" },
		{ "sfc", "Super Nintendo Entertainment System" }, { "snes", "Super Nintendo Entertainment System" },
		{ "md", "Sega Genesis" }, { "sms", "Sega Master System" }, { "gg", "Sega Game Gear" },
		{ "pce", "NEC TurboGrafx-16" }, { "ngp", "SNK Neo Geo Pocket" },
		{ "ngpc", "SNK Neo Geo Pocket Color" }, { "ps", "Sony Playstation" },
		{ "psx", "Sony Playstation" }, { "pico8", "PICO-8" }, { "p8", "PICO-8" },
		{ NULL, NULL }
	};
	int i;

	if (!tag) return NULL;
	for (i = 0; map[i].tag; i++) if (!strcasecmp(tag, map[i].tag)) return map[i].name;
	return NULL;
}

/* ---- fetching ---------------------------------------------------------------- */

static char *fetch(const char *url)
{
	char *buf = malloc(LBOX_PAGE);
	net_req r = { 0 };
	int http = 0;
	long n;

	if (!buf) return NULL;
	r.url = url;
	r.timeout_s = 15;
	r.accept = "text/html";
	n = net_request(&r, buf, LBOX_PAGE, &http);
	if (n <= 0 || http < 200 || http >= 300) {
		fprintf(stderr, "launchbox: %s: HTTP %d\n", url, http);
		free(buf);
		return NULL;
	}
	return buf;
}

int lbox_search(const char *words, const char *tag, lbox_game *out, int max)
{
	char enc[512], url[1200];
	char *html;
	lbox_game *all;
	const char *plat = lbox_platform(tag);
	int n, i, k = 0;

	net_urlencode(words, enc, sizeof enc);
	snprintf(url, sizeof url, LBOX_HOST "/games/results/%s?searchText=%s", enc, enc);
	if (!(html = fetch(url))) return -1;
	if (!(all = calloc(200, sizeof *all))) { free(html); return -1; }
	n = lbox_parse_results(html, all, 200);
	free(html);
	/* this console's games first, in the site's order, then the rest */
	for (i = 0; i < n && k < max; i++)
		if (plat && !strcasecmp(all[i].platform, plat)) out[k++] = all[i];
	for (i = 0; i < n && k < max; i++)
		if (!plat || strcasecmp(all[i].platform, plat)) out[k++] = all[i];
	free(all);
	fprintf(stderr, "launchbox: \"%s\": %d game(s)\n", words, n);
	return k;
}

int lbox_images(int id, const char *tag, lbox_image *out, int max)
{
	char url[256], *html;
	int n;

	(void)tag;
	snprintf(url, sizeof url, LBOX_HOST "/games/images/%d", id);
	if (!(html = fetch(url))) return -1;
	n = lbox_parse_images(html, out, max, NULL);
	free(html);
	fprintf(stderr, "launchbox: game %d: %d picture(s) for a cover\n", id, n);
	return n;
}
