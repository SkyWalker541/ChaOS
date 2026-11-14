/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: cheats (src/cheats.c) - reading a .cht, keeping which are on, what
 * goes to Diatom, and finding a game in the index. Files of our own, in
 * RetroArch's format; no network. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cheats.h"

/* artscrape.c is linked for its matching rule; the scraping it also does
 * reaches these, which are not wanted here (as in tools/itch-check.c) */
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
#define ck(c, ...) do { if (!(c)) { fails++; printf("  FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *p)
{
	FILE *f = fopen(p, "r");
	char *b;
	long n;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	b = calloc(1, (size_t)n + 1);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
	fclose(f);
	return b;
}

static void put(const char *p, const char *s)
{
	FILE *f = fopen(p, "w");
	fputs(s, f);
	fclose(f);
}

int main(void)
{
	const char *dir = "/tmp/chaos-cheats-check";
	char p[256], act[256], *s;
	cht_list l;

	if (system("rm -rf /tmp/chaos-cheats-check && mkdir -p /tmp/chaos-cheats-check")) return 1;
	snprintf(p, sizeof p, "%s/game.cht", dir);
	snprintf(act, sizeof act, "%s/active.txt", dir);

	/* Codes, quoted and bare, an enable missing, a value to fill in */
	put(p,
	    "cheats = 4\n\n"
	    "cheat0_desc = \"Infinite Time\"\ncheat0_code = \"7E1E6B14\"\ncheat0_enable = false\n\n"
	    "cheat1_desc = \"Max Coins\"\ncheat1_code = \"7E1E5B14+7E1E5914\"\ncheat1_enable = true\n\n"
	    "cheat2_desc = \"Pick a level\"\ncheat2_code = \"7E0100??\"\ncheat2_enable = true\n\n"
	    "cheat3_desc = Bare\ncheat3_code = ABCD-1234\n");
	ck(cht_read(p, &l) && l.n == 4, "four cheats read");
	ck(!strcmp(l.e[0].desc, "Infinite Time") && !strcmp(l.e[1].code, "7E1E5B14+7E1E5914"), "quoted values");
	ck(!strcmp(l.e[3].desc, "Bare") && !strcmp(l.e[3].code, "ABCD-1234"), "bare values");
	ck(!l.e[0].on && l.e[1].on && !l.e[3].on, "enables");
	ck(cht_usable(&l.e[0], true) && !cht_usable(&l.e[2], true), "a code asking for a value is not usable");
	ck(!cht_usable(&l.e[0], false), "nor a code where the emulator takes none");

	ck(cht_write_active(act, &l, true) == 1, "one on and usable");
	s = slurp(act);
	ck(s && !strcmp(s, "C\t7E1E5B14+7E1E5914\n"), "active file: %s", s ? s : "(none)");
	free(s);

	/* Turn one on and one off, save, read back; the rest of the file kept */
	l.e[0].on = true;
	l.e[1].on = false;
	l.e[3].on = true;
	ck(cht_save_enables(p, &l), "saved");
	cht_free(&l);
	ck(cht_read(p, &l) && l.e[0].on && !l.e[1].on && l.e[3].on, "the enables came back");
	s = slurp(p);
	ck(s && strstr(s, "cheat0_desc = \"Infinite Time\"") && strstr(s, "cheat3_enable = true"),
	   "descriptions kept, the missing enable added");
	free(s);
	cht_free(&l);

	/* Memory cheats, as RetroArch's cheat search writes them */
	put(p,
	    "cheat0_address = \"820\"\ncheat0_address_bit_position = \"0\"\ncheat0_big_endian = \"false\"\n"
	    "cheat0_cheat_type = \"1\"\ncheat0_code = \"\"\ncheat0_desc = \"Infinite health\"\ncheat0_enable = \"true\"\n"
	    "cheat0_handler = \"1\"\ncheat0_memory_search_size = \"3\"\ncheat0_repeat_add_to_address = \"1\"\n"
	    "cheat0_repeat_add_to_value = \"0\"\ncheat0_repeat_count = \"1\"\ncheat0_value = \"4\"\n"
	    "cheat1_address = \"0x10\"\ncheat1_big_endian = \"true\"\ncheat1_cheat_type = \"0\"\n"
	    "cheat1_desc = \"Rumble only\"\ncheat1_enable = \"true\"\ncheat1_handler = \"1\"\ncheat1_memory_search_size = \"4\"\n");
	ck(cht_read(p, &l) && l.n == 2, "memory cheats read, with no count line");
	ck(l.e[0].handler == 1 && l.e[0].address == 820 && l.e[0].value == 4 && l.e[0].size == 3, "fields");
	ck(cht_usable(&l.e[0], false), "memory cheats work without the emulator's codes");
	ck(!cht_usable(&l.e[1], true), "a cheat that does nothing (type 0) is not offered");
	ck(cht_write_active(act, &l, false) == 1, "one memory cheat on");
	s = slurp(act);
	ck(s && !strcmp(s, "M\t820 3 1 4 0 0 1 1 0\n"), "memory line: %s", s ? s : "(none)");
	free(s);
	cht_free(&l);

	ck(!cht_read("/nonexistent.cht", &l), "a missing file");
	ck(cht_core_takes_codes("snes9x2010") && !cht_core_takes_codes("mednafen_ngp"), "which cores take codes");

	cht_path("/mnt/SDCARD", "SNES", "Super Mario World (USA).sfc", p, sizeof p);
	ck(!strcmp(p, "/mnt/SDCARD/Cheats/SNES/Super Mario World (USA).cht"), "path: %s", p);

	/* The index */
	{
		cht_index x;
		int idx[16], n;
		char url[1024];

		ck(!cht_index_parse("Super Mario World (USA)\n", &x), "an index needs its commit line");
		ck(cht_index_parse("# 0123456789abcdef\n"
		                   "Super Mario World (Rumbles)\n"
		                   "Super Mario World (USA) (Game Genie)\n"
		                   "Super Mario World (USA)\n"
		                   "Super Mario World (World) (Action Replay)\n"
		                   "Super Mario World 2 - Yoshi's Island (USA)\n"
		                   "Zelda no Densetsu (Japan)\n", &x), "parsed");
		ck(x.n == 5 && !strcmp(x.commit, "0123456789abcdef"), "five names (a rumble file left out), the commit");
		n = cht_candidates(&x, "Super Mario World (USA).sfc", idx, 16);
		ck(n == 3, "three files are this game, not Yoshi's Island: %d", n);
		ck(n > 0 && !strcmp(x.names[idx[0]], "Super Mario World (USA)"), "the exact name first: %s",
		   n > 0 ? x.names[idx[0]] : "");
		n = cht_candidates(&x, "Kirby.sfc", idx, 16);
		ck(n == 0, "no match");
		ck(cht_nearest(&x, "Yoshi.sfc") == 4, "nearest by name: %d", cht_nearest(&x, "Yoshi.sfc"));
		cht_file_url(&x, "Nintendo - Super Nintendo Entertainment System", "Super Mario World (USA)",
		             url, sizeof url);
		ck(!strcmp(url, CHT_RAW "0123456789abcdef/cht/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/"
		                "Super%20Mario%20World%20%28USA%29.cht"), "file URL: %s", url);
		cht_index_url("Sega - Game Gear", url, sizeof url);
		ck(!strcmp(url, CHT_SITE "Sega%20-%20Game%20Gear.txt"), "index URL: %s", url);
		cht_index_free(&x);
	}

	if (fails) { printf("cheats: %d failed\n", fails); return 1; }
	printf("ok: cheats are read, kept, sent and found as they should be\n");
	return 0;
}
