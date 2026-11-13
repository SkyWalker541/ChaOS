/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: itch.io's feeds read, titles matched and the card's list kept, from a
 * feed page itch.io served (tools/fixtures/itch). No network. See src/itch.h. */
#include "../src/itch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long n;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n + 1);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
	fclose(f);
	b[n] = '\0';
	*len = (size_t)n;
	return b;
}

static int score(const char *rom, const char *title)
{
	char a[256], b[256];

	itch_title_key(rom, a, sizeof a);
	itch_title_key(title, b, sizeof b);
	return itch_score(a, b);
}

int main(void)
{
	itch_game g[ITCH_PER_PAGE];
	size_t len;
	char *xml = slurp("tools/fixtures/itch/tag-nes-rom-page1.xml", &len), key[256];
	int n, best[8], k, i;

	printf("reading a feed page\n");
	n = xml ? itch_feed_parse(xml, len, g, ITCH_PER_PAGE) : -2;
	CHECK(n == 6, "six games of eight (one off itch.io, one with no cover): %d", n);
	CHECK(n > 0 && !strcmp(g[0].title, "Witch n' Wiz (NES)"), "entities decoded: %s", n > 0 ? g[0].title : "");
	CHECK(n > 0 && !strcmp(g[0].link, "https://mhughson.itch.io/witch-n-wiz"), "the page: %s", g[0].link);
	CHECK(n > 0 && !strcmp(g[0].image, "https://img.itch.zone/aW1nLzczNTUyMTcucG5n/315x250%23c/qToidU.png"),
	      "the cover, as itch.io wrote it: %s", g[0].image);
	CHECK(n > 2 && !strcmp(g[2].title, "Böbl (NES)"), "UTF-8 kept: %s", g[2].title);
	CHECK(n > 4 && !strcmp(g[4].title, "Tobu Tobu Girl & Friends"), "a title in CDATA, from <title>: %s", g[4].title);
	CHECK(n > 5 && !strcmp(g[5].title, "Tab Title"), "a tab made a space: [%s]", g[5].title);
	for (i = 0; i < n; i++)
		CHECK(strcmp(g[i].title, "Elsewhere") && strcmp(g[i].title, "No Cover"), "left out: %s", g[i].title);
	CHECK(itch_feed_parse("<html>no</html>", strlen("<html>no</html>"), g, 4) == -1, "a page that isn't a feed");
	CHECK(itch_feed_parse("<rss><item><plainTitle>x", strlen("<rss><item><plainTitle>x"), g, 4) == 0, "a cut-off feed reads none");

	printf("titles as compared\n");
	itch_title_key("Super Tilt Bro. for NES", key, sizeof key);
	CHECK(!strcmp(key, "super tilt bro"), "\"for NES\" goes: %s", key);
	itch_title_key("FROM BELOW - A Real NES Game!", key, sizeof key);
	CHECK(!strcmp(key, "from below"), "\"a real NES game\" goes: %s", key);
	itch_title_key("Deadeus (Game Boy Color)", key, sizeof key);
	CHECK(!strcmp(key, "deadeus"), "a tag in brackets goes: %s", key);
	itch_title_key("Opossum Country - Game Boy Edition", key, sizeof key);
	CHECK(!strcmp(key, "opossum country"), "\"Game Boy edition\" goes: %s", key);
	itch_title_key("GB Studio Homebrew Game Jam Entry", key, sizeof key);
	CHECK(!strcmp(key, "game jam entry"), "console words at the front go: %s", key);
	itch_title_key("NES ROM", key, sizeof key);
	CHECK(key[0], "a title of console words alone is kept: %s", key);
	itch_title_key("The Game Gear Game", key, sizeof key);
	CHECK(strstr(key, "game") != NULL, "\"game\" stays when it is the name's: %s", key);

	printf("scores\n");
	CHECK(score("Super Tilt Bro", "Super Tilt Bro. for NES") == 100, "the same game: %d", score("Super Tilt Bro", "Super Tilt Bro. for NES"));
	CHECK(score("Bobl", "Böbl (NES)") == 100, "accents folded: %d", score("Bobl", "Böbl (NES)"));
	CHECK(score("TobuTobuGirl", "Tobu Tobu Girl") == 95, "written together: %d", score("TobuTobuGirl", "Tobu Tobu Girl"));
	CHECK(score("Deadeus", "Deadeus: The Game Boy Horror") >= 70, "the name inside a longer title: %d", score("Deadeus", "Deadeus: The Game Boy Horror"));
	CHECK(score("Witch n Wiz (USA)", "Witch n' Wiz (NES)") == 100, "a region tag on the ROM: %d", score("Witch n Wiz (USA)", "Witch n' Wiz (NES)"));
	CHECK(score("Tetris", "Super Tilt Bro. for NES") == 0, "nothing shared: %d", score("Tetris", "Super Tilt Bro. for NES"));
	CHECK(score("Super Mario Bros", "Super Tilt Bro. for NES") < ITCH_SCORE_MIN, "one word shared of five: %d", score("Super Mario Bros", "Super Tilt Bro. for NES"));
	CHECK(score("gb-studio-export", "Deadeus") == 0, "an export's name finds nothing: %d", score("gb-studio-export", "Deadeus"));

	printf("matches\n");
	k = itch_match("Super Tilt Bro (Homebrew)", g, n, best, 8);
	CHECK(k >= 1 && !strcmp(g[best[0]].title, "Super Tilt Bro. for NES"), "found, first: %d", k);
	k = itch_match("From Below", g, n, best, 8);
	CHECK(k == 1 && !strcmp(g[best[0]].link, "https://mhughson.itch.io/from-below"), "one match: %d", k);
	k = itch_match("Contra", g, n, best, 8);
	CHECK(k == 0, "none for a game not there: %d", k);
	{
		itch_game twice[3];

		twice[0] = g[1]; twice[1] = g[1]; twice[2] = g[0];
		k = itch_match("Super Tilt Bro", twice, 3, best, 8);
		CHECK(k == 1, "the same page under two tags, once: %d", k);
	}

	printf("the list on the card\n");
	{
		char path[] = "/tmp/chaos-itch-check-XXXXXX";
		int fd = mkstemp(path), m;
		time_t when;
		bool whole;
		itch_game *back;

		close(fd);
		CHECK(itch_index_save(path, g, n, 1762987654, true), "saved");
		back = itch_index_load(path, &m, &when, &whole);
		CHECK(back && m == n, "read back whole: %d of %d", m, n);
		CHECK(when == 1762987654 && whole, "when, and whole");
		for (i = 0; back && i < m && i < n; i++)
			CHECK(!strcmp(back[i].title, g[i].title) && !strcmp(back[i].link, g[i].link) &&
			      !strcmp(back[i].image, g[i].image), "game %d the same", i);
		free(back);
		{
			FILE *f = fopen(path, "w");
			fputs("not a list\n", f);
			fclose(f);
		}
		back = itch_index_load(path, &m, &when, &whole);
		CHECK(!back && m == 0, "a file that isn't one reads as none");
		{
			FILE *f = fopen(path, "w");
			fputs("#chaos-itch 1 5 part\nA\thttps://evil.example/x\thttps://img.itch.zone/a.png\n"
			      "B\thttps://b.itch.io/b\thttps://img.itch.zone/b.png\nbroken line\n", f);
			fclose(f);
		}
		back = itch_index_load(path, &m, &when, &whole);
		CHECK(back && m == 1 && !strcmp(back[0].title, "B") && !whole,
		      "lines off itch.io, or broken, are skipped; a part list says so: %d", m);
		free(back);
		unlink(path);
	}

	printf("which feeds\n");
	CHECK(itch_slugs("NES") && !strcmp(itch_slugs("NES")[0], "tag-nes-rom"), "NES");
	CHECK(itch_slugs("Game Boy") && itch_slugs("Game Boy")[2] && !itch_slugs("Game Boy")[3], "Game Boy's three");
	CHECK(!itch_slugs("Neo Geo Pocket"), "none for a console itch.io has no tag for");

	free(xml);
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("ok: every itch.io check passed\n");
	return 0;
}
