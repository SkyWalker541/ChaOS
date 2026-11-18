/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Get Homebrew's catalogue, a game's page, content warnings
 * (hbcat.h). No SDL, no network. */

#include "hbcat.h"
#include "rajson.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *hb_sort_name(hb_sort s)
{
	static const char *const N[HB_SORTS] = { "Popular", "Newest", "A-Z", "Z-A", "Downloaded First" };

	return s >= 0 && s < HB_SORTS ? N[s] : N[0];
}

const char *hb_show_name(hb_show s)
{
	static const char *const N[HB_SHOWS] = { "All Games", "Free", "Paid", "Owned", "On This Brick" };

	return s >= 0 && s < HB_SHOWS ? N[s] : N[0];
}

void hb_author_of(const char *link, char *out, size_t n)
{
	const char *by = strstr(link, "://"), *dot;

	out[0] = '\0';
	if (!by) return;
	by += 3;
	if (!(dot = strstr(by, ".itch.io")) || dot == by || (size_t)(dot - by) >= n) return;
	memcpy(out, by, (size_t)(dot - by));
	out[dot - by] = '\0';
}

/* ---- search ---- */

static bool has_word(const char *hay, const char *w, size_t wl)
{
	size_t hl = strlen(hay), i;

	for (i = 0; i + wl <= hl; i++)
		if (!strncasecmp(hay + i, w, wl)) return true;
	return false;
}

bool hb_matches(const hb_game *g, const char *search)
{
	const char *p = search;

	if (!search) return true;
	while (*p) {
		size_t wl;

		while (*p == ' ') p++;
		if (!*p) break;
		wl = strcspn(p, " ");
		if (!has_word(g->g.title, p, wl) && !has_word(g->author, p, wl)) return false;
		p += wl;
	}
	return true;
}

/* ---- sorting ---- */

/* A title as it sorts: from its first letter or digit, any case. Leading
 * quotes, brackets and emoji don't count. */
static const char *sort_from(const char *t)
{
	while (*t && !isalnum((unsigned char)*t)) t++;
	return t;
}

char hb_initial(const char *title)
{
	unsigned char c = (unsigned char)*sort_from(title);

	return isalpha(c) ? (char)toupper(c) : '#';
}

static const hb_game *S_G;
static hb_sort S_SORT;

static int by_title(const hb_game *a, const hb_game *b)
{
	int c = strcasecmp(sort_from(a->g.title), sort_from(b->g.title));

	return c ? c : strcmp(a->g.link, b->g.link);
}

static int by_pop(const hb_game *a, const hb_game *b)
{
	if (a->order != b->order) return a->order - b->order;
	if (a->rank != b->rank) return a->rank - b->rank;
	return a->sys - b->sys;
}

/* undated last (a list read before 2.6.0 has no dates), in itch.io's order */
static int by_new(const hb_game *a, const hb_game *b)
{
	if (a->g.pub != b->g.pub) return a->g.pub > b->g.pub ? -1 : 1;
	return by_pop(a, b);
}

static int dl_group(const hb_game *g)
{
	if (g->flags & (HB_UPDATE | HB_GONE)) return 0;
	if (g->flags & HB_HAVE) return 1;
	return 2;
}

static int cmp(const void *x, const void *y)
{
	const hb_game *a = &S_G[*(const int *)x], *b = &S_G[*(const int *)y];

	switch (S_SORT) {
	case HB_SORT_AZ: return by_title(a, b);
	case HB_SORT_ZA: return by_title(b, a);
	case HB_SORT_DL: {
		int ga = dl_group(a), gb = dl_group(b);

		if (ga != gb) return ga - gb;
		return ga < 2 ? by_title(a, b) : by_pop(a, b);
	}
	case HB_SORT_NEW: return by_new(a, b);
	default: return by_pop(a, b);
	}
}

int hb_view_apply(const hb_game *games, int n, const hb_view *v, int *out)
{
	int i, k = 0;

	for (i = 0; i < n; i++) {
		const hb_game *g = &games[i];

		if (v->sys >= 0 && g->sys != v->sys) continue;
		switch (v->show) {
		case HB_SHOW_FREE:  if (!itch_free(&g->g)) continue; break;
		case HB_SHOW_PAID:  if (itch_free(&g->g)) continue; break;
		case HB_SHOW_OWNED: if (!(g->flags & HB_OWNED)) continue; break;
		case HB_SHOW_HAVE:  if (!(g->flags & HB_HAVE)) continue; break;
		default: break;
		}
		if (v->search[0] && !hb_matches(g, v->search)) continue;
		out[k++] = i;
	}
	S_G = games;
	S_SORT = v->sort;
	qsort(out, (size_t)k, sizeof *out, cmp);
	return k;
}

/* ---- data.json ---- */

static void str_member(jsv o, const char *name, char *out, size_t n)
{
	jsv v;

	out[0] = '\0';
	if (js_member(o, name, &v)) js_str(v, out, n);
}

bool hb_data_parse(const char *json, size_t len, hb_detail *d)
{
	jsv root = js_root(json, len), v, it, e;

	memset(d, 0, sizeof *d);
	if (!js_member(root, "title", &v) && !js_member(root, "id", &v)) return false;
	str_member(root, "price", d->price, sizeof d->price);
	str_member(root, "suggested_price", d->suggested, sizeof d->suggested);
	str_member(root, "original_price", d->original, sizeof d->original);
	str_member(root, "cover_image", d->cover, sizeof d->cover);
	if (js_member(root, "sale", &v)) {
		jsv r;

		if (js_member(v, "rate", &r)) d->sale = (int)js_int(r);
	}
	if (js_member(root, "screenshots", &v)) {
		memset(&it, 0, sizeof it);
		while (d->nshots < HB_SHOTS && js_next(v, &it, &e))
			if (js_str(e, d->shots[d->nshots], sizeof d->shots[0]) &&
			    !strncmp(d->shots[d->nshots], "https://", 8))
				d->nshots++;
	}
	if (js_member(root, "tags", &v)) {
		memset(&it, 0, sizeof it);
		while (d->ntags < HB_TAGS && js_next(v, &it, &e))
			if (js_str(e, d->tags[d->ntags], sizeof d->tags[0]) && d->tags[d->ntags][0])
				d->ntags++;
	}
	if (js_member(root, "authors", &v)) {
		memset(&it, 0, sizeof it);
		while (js_next(v, &it, &e)) {
			char name[64];
			size_t l = strlen(d->authors);

			str_member(e, "name", name, sizeof name);
			if (!name[0]) continue;
			snprintf(d->authors + l, sizeof d->authors - l, "%s%s", l ? ", " : "", name);
		}
	}
	return true;
}

static bool zero_amount(const char *p)
{
	for (; *p; p++) if (*p >= '1' && *p <= '9') return false;
	return true;
}

void hb_price_line(const hb_detail *d, char *out, size_t n)
{
	if (!d->price[0]) snprintf(out, n, "Free");
	else if (zero_amount(d->price) && d->suggested[0] && !zero_amount(d->suggested))
		snprintf(out, n, "Name your own price, %s suggested", d->suggested);
	else if (zero_amount(d->price)) snprintf(out, n, "Name your own price");
	else if (d->sale > 0 && d->original[0])
		snprintf(out, n, "%s (was %s, %d%% off)", d->price, d->original, d->sale);
	else snprintf(out, n, "%s", d->price);
}

/* ---- the description ---- */

typedef struct { char *out; size_t n, o; bool sp, nl; int blank; } wr;

static void put_c(wr *w, char c)
{
	if (w->o + 1 < w->n) w->out[w->o++] = c;
	w->out[w->o < w->n ? w->o : w->n - 1] = '\0';
}

/* a line break: `blank` 1 for a new line, 2 for a blank one between */
static void put_break(wr *w, int blank)
{
	if (w->o == 0) return;
	while (w->o && w->out[w->o - 1] == ' ') w->o--;
	if (blank > w->blank) {
		int k;

		for (k = w->blank; k < blank; k++) put_c(w, '\n');
		w->blank = blank;
	}
	w->sp = true;
}

static void put_text(wr *w, const char *s, size_t l)
{
	size_t i;

	for (i = 0; i < l; i++) {
		unsigned char c = (unsigned char)s[i];

		if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
			if (!w->sp && w->o) { put_c(w, ' '); w->sp = true; }
			continue;
		}
		put_c(w, (char)c);
		w->sp = false;
		w->blank = 0;
	}
}

static void put_utf8(wr *w, unsigned cp)
{
	char b[4];
	size_t l;

	if (cp == 0xA0) cp = ' ';
	if (cp < 0x80) { b[0] = (char)cp; l = 1; }
	else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); l = 2; }
	else if (cp < 0x10000) {
		b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 63)); b[2] = (char)(0x80 | (cp & 63)); l = 3;
	} else {
		b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | (cp >> 12 & 63));
		b[2] = (char)(0x80 | (cp >> 6 & 63)); b[3] = (char)(0x80 | (cp & 63)); l = 4;
	}
	put_text(w, b, l);
}

/* an entity at p (after '&'): its length, written */
static size_t entity(wr *w, const char *p)
{
	static const struct { const char *n; unsigned cp; } E[] = {
		{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
		{ "nbsp", 0xA0 }, { "rsquo", 0x2019 }, { "lsquo", 0x2018 }, { "rdquo", 0x201D },
		{ "ldquo", 0x201C }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 },
		{ "copy", 0xA9 }, { "trade", 0x2122 }, { "eacute", 0xE9 },
	};
	const char *semi = strchr(p, ';');
	size_t l, i;

	if (!semi || semi - p > 10) { put_text(w, "&", 1); return 0; }
	l = (size_t)(semi - p);
	if (p[0] == '#') {
		unsigned cp = p[1] == 'x' || p[1] == 'X' ? (unsigned)strtoul(p + 2, NULL, 16)
		                                         : (unsigned)strtoul(p + 1, NULL, 10);

		if (cp) put_utf8(w, cp);
		return l + 1;
	}
	for (i = 0; i < sizeof E / sizeof E[0]; i++)
		if (strlen(E[i].n) == l && !strncmp(p, E[i].n, l)) { put_utf8(w, E[i].cp); return l + 1; }
	put_text(w, "&", 1);
	return 0;
}

static bool tag_is(const char *t, const char *name)
{
	size_t l = strlen(name);

	if (*t == '/') t++;
	return !strncasecmp(t, name, l) && (t[l] == '>' || t[l] == ' ' || t[l] == '/' || t[l] == '\t' ||
	                                    t[l] == '\n');
}

size_t hb_desc_parse(const char *html, char *out, size_t n)
{
	const char *p = html ? strstr(html, "formatted_description") : NULL, *q;
	wr w = { out, n, 0, true, false, 2 };
	int depth = 0;
	const char *skip = NULL;

	if (!n) return 0;
	out[0] = '\0';
	if (!p || !(p = strchr(p, '>'))) return 0;
	p++;
	depth = 1;
	while (*p && depth > 0) {
		if (*p == '<') {
			const char *t = p + 1;
			bool close = *t == '/';

			if (!strncmp(p, "<!--", 4)) {
				q = strstr(p, "-->");
				p = q ? q + 3 : p + strlen(p);
				continue;
			}
			q = strchr(p, '>');
			if (!q) break;
			if (skip) {
				if (close && tag_is(t, skip)) skip = NULL;
				p = q + 1;
				continue;
			}
			if (tag_is(t, "div")) {
				if (close) depth--;
				else if (q[-1] != '/') depth++;
				put_break(&w, 2);
			} else if (tag_is(t, "script")) skip = "script";
			else if (tag_is(t, "style"))  skip = "style";
			else if (tag_is(t, "iframe")) skip = "iframe";
			else if (tag_is(t, "button")) skip = "button";
			else if (tag_is(t, "br")) put_break(&w, 1);
			else if (tag_is(t, "p") || tag_is(t, "h1") || tag_is(t, "h2") || tag_is(t, "h3") ||
			         tag_is(t, "h4") || tag_is(t, "h5") || tag_is(t, "h6") || tag_is(t, "ul") ||
			         tag_is(t, "ol") || tag_is(t, "blockquote") || tag_is(t, "hr") || tag_is(t, "table"))
				put_break(&w, 2);
			else if (tag_is(t, "li") || tag_is(t, "tr")) {
				put_break(&w, 1);
				if (!close && tag_is(t, "li")) { put_text(&w, "- ", 2); w.sp = true; }
			}
			p = q + 1;
			continue;
		}
		if (skip) { p++; continue; }
		if (*p == '&') { p += 1 + entity(&w, p + 1); continue; }
		q = p;
		while (*q && *q != '<' && *q != '&') q++;
		put_text(&w, p, (size_t)(q - p));
		p = q;
	}
	while (w.o && (out[w.o - 1] == '\n' || out[w.o - 1] == ' ')) w.o--;
	out[w.o] = '\0';
	return w.o;
}

/* ---- content warnings ---- */

/* As the pak has them (internal/itchio/advisory.go), itch.io's own tags */
static const char *const CW_ADULT[] = {
	"adult", "boobs", "ecchi", "eroge", "erotic", "femdom", "gore", "hentai", "innuendo", "lewd",
	"nsfw", "nudity", "porn", "sexual-content", "sexy", "softcore", "suggestive", "xxx", NULL
};
static const char *const CW_HEAVY[] = {
	"abuse", "anxiety", "bereavement", "child-loss", "death", "depression", "domestic-abuse",
	"eating-disorder", "grief", "loss", "mental-health", "mental-illness", "miscarriage",
	"self-harm", "sexual-assault", "suicide", "trauma", "war", NULL
};
static const char *const CW_SUBSTANCE[] = {
	"addiction", "alcohol", "drug-use", "drugs", "substance-abuse", NULL
};

const char *hb_cw_name(hb_cw c)
{
	return c == HB_CW_ADULT ? "Adult Content" : c == HB_CW_HEAVY ? "Heavy Themes" : "Drugs and Alcohol";
}

const char *hb_cw_hint(hb_cw c)
{
	return c == HB_CW_ADULT ? "Sexual content, nudity, gore"
	     : c == HB_CW_HEAVY ? "Abuse, grief, self-harm, war"
	                        : "Drug and alcohol use";
}

const char *const *hb_cw_tags(hb_cw c)
{
	return c == HB_CW_ADULT ? CW_ADULT : c == HB_CW_HEAVY ? CW_HEAVY : CW_SUBSTANCE;
}

bool hb_cw_default(hb_cw c)
{
	(void)c;
	return true;
}

/* itch.io's tags come as words ("Sexual Content") or slugs ("sexual-content") */
static bool same_tag(const char *tag, const char *slug)
{
	for (; *tag && *slug; tag++, slug++) {
		char a = (char)tolower((unsigned char)*tag);

		if (a == ' ') a = '-';
		if (a != *slug) return false;
	}
	return !*tag && !*slug;
}

int hb_cw_hits(const hb_detail *d, unsigned on, bool (*off)(const char *tag, void *ctx), void *ctx,
               char *out, size_t n)
{
	int c, i, k = 0;

	if (n) out[0] = '\0';
	for (i = 0; i < d->ntags; i++) {
		bool hit = false;

		for (c = 0; c < HB_CWS && !hit; c++) {
			const char *const *t;

			if (!(on & (1u << c))) continue;
			for (t = hb_cw_tags((hb_cw)c); *t && !hit; t++)
				hit = same_tag(d->tags[i], *t) && !(off && off(*t, ctx));
		}
		if (hit) {
			size_t l = n ? strlen(out) : 0;

			if (n) snprintf(out + l, n - l, "%s%s", k ? ", " : "", d->tags[i]);
			k++;
		}
	}
	return k;
}

/* ---- downloaded.list ---- */

bool hb_have_line(const char *line, hb_have_rec *r)
{
	char buf[1024], *f[4] = { 0 }, *p = buf;
	int k = 0;

	memset(r, 0, sizeof *r);
	snprintf(buf, sizeof buf, "%s", line);
	buf[strcspn(buf, "\r\n")] = '\0';
	while (k < 4) {
		f[k++] = p;
		if (!(p = strchr(p, '\t'))) break;
		*p++ = '\0';
	}
	if (k < 2 || !f[0][0] || !f[1][0]) return false;
	snprintf(r->link, sizeof r->link, "%s", f[0]);
	snprintf(r->rel, sizeof r->rel, "%s", f[1]);
	if (k > 2) snprintf(r->upload, sizeof r->upload, "%s", f[2]);
	if (k > 3) r->size = atol(f[3]);
	return true;
}
