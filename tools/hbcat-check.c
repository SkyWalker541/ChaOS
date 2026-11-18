/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Get Homebrew's catalogue (src/hbcat.c): the view's filters, search
 * and sorts, a game's data.json and description, content warnings, and
 * downloaded.list's lines. make check-hbcat */

#include "hbcat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* artscrape.c, which itch.c uses, wants these; nothing here calls them */

/* Never called: artscrape.c (for art_norm_loose) links against the
 * ScreenScraper client, as tools/artscrape-check.c says. */
#include <stdbool.h>
bool ss_signed_in(void) { return false; }
bool ss_run_begin(const char *a, const char *b, const char *c, const char *d,
                  const char *e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }
int  ss_run_step(void) { return -1; }
void ss_run_cancel(void) { }
int  ss_run_left(void) { return -1; }
bool ss_run_too_many(void) { return false; }
void art_shrink(const char *path) { (void)path; }



static int fails;

static void ck(int ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	if (!ok) fails++;
}

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long l;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	l = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)l + 1);
	if (b && fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); b = NULL; }
	if (b) b[l] = '\0';
	fclose(f);
	if (len) *len = (size_t)l;
	return b;
}

static hb_game mk(const char *title, const char *link, const char *price, long long pub, int sys,
                  int order, unsigned flags)
{
	hb_game g;

	memset(&g, 0, sizeof g);
	snprintf(g.g.title, sizeof g.g.title, "%s", title);
	snprintf(g.g.link, sizeof g.g.link, "%s", link);
	snprintf(g.g.price, sizeof g.g.price, "%s", price);
	g.g.pub = pub;
	g.sys = sys;
	g.order = order;
	g.flags = (unsigned char)flags;
	hb_author_of(link, g.author, sizeof g.author);
	return g;
}

static bool off_gore(const char *tag, void *ctx)
{
	(void)ctx;
	return !strcmp(tag, "gore");
}

int main(void)
{
	hb_game g[6];
	hb_view v;
	int idx[6], n;
	char a[64];

	/* dates */
	ck(itch_date("Tue, 16 May 2017 04:22:41 GMT") == 1494908561LL, "an RSS pubDate, in GMT");
	ck(itch_date("Tue, 16 May 2017 06:22:41 +0200") == 1494908561LL, "and with an offset");
	ck(itch_date("yesterday") == 0, "and not a date: 0");

	hb_author_of("https://sgadrat.itch.io/super-tilt-bro", a, sizeof a);
	ck(!strcmp(a, "sgadrat"), "the author, from the address");

	g[0] = mk("Super Tilt Bro.", "https://sgadrat.itch.io/super-tilt-bro", "$0.00", 300, 1, 0, HB_HAVE);
	g[1] = mk("\"Alpha\" Quest", "https://zed.itch.io/alpha", "$4.99", 500, 1, 1, HB_OWNED);
	g[2] = mk("Böbl", "https://morphcatgames.itch.io/bobl", "", 100, 2, 0, 0);
	g[3] = mk("Witch n' Wiz", "https://mhughson.itch.io/wiz", "$0.00", 400, 2, 1, HB_HAVE | HB_UPDATE);
	g[4] = mk("2048 Boy", "https://zed.itch.io/2048", "$1.00", 0, 1, 2, 0);
	g[5] = mk("Tobu Tobu Girl", "https://tangramgames.itch.io/tobutobugirl", "$0.00", 200, 2, 2, HB_HAVE | HB_GONE);

	memset(&v, 0, sizeof v);
	v.sys = -1;
	n = hb_view_apply(g, 6, &v, idx);
	ck(n == 6 && idx[0] == 0 && idx[1] == 2 && idx[2] == 1 && idx[3] == 3 && idx[4] == 4 && idx[5] == 5,
	   "Popular: itch.io's order, each list's first games first");
	v.sort = HB_SORT_NEW;
	n = hb_view_apply(g, 6, &v, idx);
	ck(n == 6 && idx[0] == 1 && idx[1] == 3 && idx[2] == 0 && idx[5] == 4,
	   "Newest: by the date it was published, undated last");
	v.sort = HB_SORT_AZ;
	n = hb_view_apply(g, 6, &v, idx);
	ck(n == 6 && idx[0] == 4 && idx[1] == 1 && idx[2] == 2 && idx[5] == 3,
	   "A-Z: by the first letter or digit, quotes aside, any case");
	ck(hb_initial(g[1].g.title) == 'A' && hb_initial(g[4].g.title) == '#', "and its letter, # for a digit");
	v.sort = HB_SORT_ZA;
	n = hb_view_apply(g, 6, &v, idx);
	ck(n == 6 && idx[0] == 3 && idx[5] == 4, "Z-A: the other way");
	v.sort = HB_SORT_DL;
	n = hb_view_apply(g, 6, &v, idx);
	ck(n == 6 && idx[0] == 5 && idx[1] == 3 && idx[2] == 0 && idx[3] == 2,
	   "Downloaded First: updates and removed games, then the rest downloaded, then itch.io's order");
	v.sort = HB_SORT_NEW;
	v.show = HB_SHOW_FREE;
	n = hb_view_apply(g, 6, &v, idx);
	ck(n == 4, "Free: no price, or $0.00");
	v.show = HB_SHOW_PAID;
	ck(hb_view_apply(g, 6, &v, idx) == 2, "Paid: the rest");
	v.show = HB_SHOW_OWNED;
	ck(hb_view_apply(g, 6, &v, idx) == 1 && idx[0] == 1, "Owned: bought by the key's account");
	v.show = HB_SHOW_HAVE;
	ck(hb_view_apply(g, 6, &v, idx) == 3, "On This Brick");
	v.show = HB_SHOW_ALL;
	v.sys = 2;
	ck(hb_view_apply(g, 6, &v, idx) == 3, "one console");
	v.sys = -1;
	snprintf(v.search, sizeof v.search, "tilt");
	ck(hb_view_apply(g, 6, &v, idx) == 1 && idx[0] == 0, "search: a word of the title, any case");
	snprintf(v.search, sizeof v.search, "ZED");
	ck(hb_view_apply(g, 6, &v, idx) == 2, "search: the author");
	snprintf(v.search, sizeof v.search, "zed boy");
	ck(hb_view_apply(g, 6, &v, idx) == 1 && idx[0] == 4, "search: every word");
	snprintf(v.search, sizeof v.search, "nothing like it");
	ck(hb_view_apply(g, 6, &v, idx) == 0, "search: none");

	/* data.json */
	{
		size_t l;
		char *j = slurp("tools/fixtures/itch/pak-data_nyop.json", &l), line[96];
		hb_detail d;

		ck(j && hb_data_parse(j, l, &d), "a game's data.json reads");
		ck(d.nshots == 4 && !strncmp(d.shots[0], "https://img.itch.zone/", 22), "its screenshots");
		ck(d.ntags == 11 && !strcmp(d.tags[7], "horror"), "its tags");
		ck(!strcmp(d.authors, "BenJelter"), "its author");
		hb_price_line(&d, line, sizeof line);
		ck(!strcmp(line, "Name your own price, $2.00 suggested"), "name your own price, with what's suggested");
		free(j);
		j = slurp("tools/fixtures/itch/pak-data_free.json", &l);
		ck(j && hb_data_parse(j, l, &d), "a free game's");
		hb_price_line(&d, line, sizeof line);
		ck(!strcmp(line, "Free"), "free");
		free(j);
		j = slurp("tools/fixtures/itch/pak-data_paid.json", &l);
		ck(j && hb_data_parse(j, l, &d), "a paid game's");
		hb_price_line(&d, line, sizeof line);
		printf("   paid: %s\n", line);
		ck(line[0] == '$' || strchr(line, '$'), "paid: its price");
		free(j);
		{
			const char *sale = "{\"title\":\"X\",\"price\":\"$2.99\",\"original_price\":\"$4.99\",\"sale\":{\"rate\":40}}";

			ck(hb_data_parse(sale, strlen(sale), &d), "a game on sale");
			hb_price_line(&d, line, sizeof line);
			ck(!strcmp(line, "$2.99 (was $4.99, 40% off)"), "on sale: the price, what it was, how much off");
		}
		ck(!hb_data_parse("<html>", 6, &d), "and not data.json: false");
	}

	/* the description */
	{
		char *h = slurp("tools/fixtures/itch/pak-game_page_live_free.html", NULL);
		char *t = malloc(16384);
		size_t l = h && t ? hb_desc_parse(h, t, 16384) : 0;

		ck(l > 200, "a page's description");
		ck(t && !strncmp(t, "The Treasure of Crisscross Cove is a GBJam 14 entry by Leafthief and iLKke.", 74),
		   "as text: entities decoded, links' words kept");
		ck(t && strstr(t, "\n- Same Longitude as thing refers"), "list items as - lines");
		ck(t && strstr(t, "Movement\n[Alt]"), "a <br> a line");
		ck(t && strstr(t, "iLKke.\n\n--- IMPORTANT"), "paragraphs a blank line apart");
		ck(t && !strstr(t, "<") && !strstr(t, "&nbsp;") && !strstr(t, "&#x27;"), "no tags, no entities");
		ck(t && strstr(t, "doesn't have time"), "&#x27; is an apostrophe");
		ck(t && !strstr(t, "\n\n\n"), "never two blank lines");
		if (t) printf("   ---\n%.600s\n   ---\n", t);
		ck(hb_desc_parse("<p>nothing</p>", t, 100) == 0, "a page with none: 0");
		free(h);
		free(t);
	}

	/* content warnings */
	{
		hb_detail d;
		char hit[160];

		memset(&d, 0, sizeof d);
		snprintf(d.tags[0], sizeof d.tags[0], "horror");
		snprintf(d.tags[1], sizeof d.tags[1], "Gore");
		snprintf(d.tags[2], sizeof d.tags[2], "Mental Health");
		d.ntags = 3;
		ck(hb_cw_hits(&d, 7, NULL, NULL, hit, sizeof hit) == 2 && !strcmp(hit, "Gore, Mental Health"),
		   "the tags that warn, in words or slugs");
		ck(hb_cw_hits(&d, 1u << HB_CW_HEAVY, NULL, NULL, hit, sizeof hit) == 1, "only the categories on");
		ck(hb_cw_hits(&d, 7, off_gore, NULL, hit, sizeof hit) == 1 && !strcmp(hit, "Mental Health"),
		   "and not a tag turned off");
		ck(hb_cw_hits(&d, 0, NULL, NULL, hit, sizeof hit) == 0, "none on: none");
	}

	/* downloaded.list */
	{
		hb_have_rec r;

		ck(hb_have_line("https://a.itch.io/b\tGame Boy/B.gb\n", &r) && !strcmp(r.rel, "Game Boy/B.gb") &&
		   !r.upload[0] && r.size == 0, "a line from before: the page and the file");
		ck(hb_have_line("https://a.itch.io/b\tGame Boy/B.gb\t12345\t65536\n", &r) &&
		   !strcmp(r.upload, "12345") && r.size == 65536, "a line now: and the upload and its size");
		ck(!hb_have_line("nonsense\n", &r), "not a line: false");
	}

	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("ok: every check passed\n");
	return 0;
}
