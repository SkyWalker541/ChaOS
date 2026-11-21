/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: multi-disc games found and made playlists, on a card made in /tmp.
 * See src/discset.h. */
#include "../src/discset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");

	if (f) { fputs(text, f); fclose(f); }
}

static char *slurp(const char *path)
{
	static char b[4096];
	FILE *f = fopen(path, "r");
	size_t n;

	b[0] = '\0';
	if (!f) return b;
	n = fread(b, 1, sizeof b - 1, f);
	b[n] = '\0';
	fclose(f);
	return b;
}

static void disc(const char *name, int want, const char *base)
{
	char b[256];
	int n = ds_disc_of(name, b, sizeof b);

	CHECK(n == want && (!want || !strcmp(b, base)), "\"%s\": disc %d \"%s\", wanted %d \"%s\"",
	      name, n, b, want, base);
}

int main(void)
{
	char root[] = "/tmp/discset-check-XXXXXX", p[1024];
	ds_set s[DS_MAX];
	int n, i;

	printf("a disc by its name\n");
	disc("Final Fantasy VII (USA) (Disc 1).chd", 1, "Final Fantasy VII (USA)");
	disc("Final Fantasy VII (USA) (Disc 3).chd", 3, "Final Fantasy VII (USA)");
	disc("Metal Gear Solid (USA) (Disc 2) (Rev 1).cue", 2, "Metal Gear Solid (USA) (Rev 1)");
	disc("Riven - Disk 4 of 5.cue", 4, "Riven");
	disc("Xenogears [Disc2of2].chd", 2, "Xenogears");
	disc("Parasite Eve CD1.chd", 1, "Parasite Eve");
	disc("Chrono Cross (Disc B).pbp", 2, "Chrono Cross");
	disc("Discworld (Europe).chd", 0, "");
	disc("Arcade Classics (CD).chd", 0, "");
	disc("Tekken 3 (USA).chd", 0, "");
	disc("Lunar - Silver Star Story Complete (Disc 1) (Making of Disc).chd", 1,
	     "Lunar - Silver Star Story Complete (Making of Disc)");

	printf("a card's PlayStation folder\n");
	if (!mkdtemp(root)) { printf("no temp folder\n"); return 1; }
	snprintf(p, sizeof p, "%s/roms/PlayStation/Chrono Cross", root); 
	{
		char cmd[1200];

		snprintf(cmd, sizeof cmd, "mkdir -p '%s' '%s/roms/PlayStation/.media' '%s/card/Saves' "
		         "'%s/card/Saves/States/PlayStation' '%s/roms/PlayStation/Done'", p, root, root, root, root);
		if (system(cmd) != 0) return 1;
	}
#define R(f) (snprintf(p, sizeof p, "%s/roms/PlayStation/%s", root, f), p)
	put(R("Final Fantasy VII (USA) (Disc 1).chd"), "1");
	put(R("Final Fantasy VII (USA) (Disc 2).chd"), "2");
	put(R("Final Fantasy VII (USA) (Disc 3).chd"), "3");
	put(R("Metal Gear Solid (USA) (Disc 1).cue"), "FILE \"Metal Gear Solid (USA) (Disc 1).bin\" BINARY\n");
	put(R("Metal Gear Solid (USA) (Disc 1).bin"), "x");
	put(R("Metal Gear Solid (USA) (Disc 2).cue"), "FILE \"Metal Gear Solid (USA) (Disc 2) (Track 1).bin\" BINARY\n"
	                                              "FILE \"Metal Gear Solid (USA) (Disc 2) (Track 2).bin\" BINARY\n");
	put(R("Metal Gear Solid (USA) (Disc 2) (Track 1).bin"), "x");
	put(R("Metal Gear Solid (USA) (Disc 2) (Track 2).bin"), "x");
	put(R("Tekken 3 (USA).chd"), "t");
	put(R("Spyro (USA) (Disc 1).chd"), "only one disc");
	put(R("Chrono Cross/Chrono Cross (Disc 1).chd"), "1");
	put(R("Chrono Cross/Chrono Cross (Disc 2).chd"), "2");
	put(R("Done/Done (Disc 1).chd"), "1");
	put(R("Done/Done (Disc 2).chd"), "2");
	put(R("Done/Done.m3u"), "Done (Disc 1).chd\nDone (Disc 2).chd\n");
	put(R(".media/Final Fantasy VII (USA) (Disc 1).png"), "art");
	snprintf(p, sizeof p, "%s/card/Saves/Final Fantasy VII (USA) (Disc 1).srm", root);
	put(p, "save");
	snprintf(p, sizeof p, "%s/card/Saves/States/PlayStation/Final Fantasy VII (USA) (Disc 1).chd.1.state", root);
	put(p, "state");
#undef R
	{
		char roms[1024];

		snprintf(roms, sizeof roms, "%s/roms", root);
		n = ds_scan(roms, "PlayStation", s, DS_MAX);
		CHECK(n == 3, "three sets (FF7, MGS loose; Chrono Cross's folder), got %d", n);
		for (i = 0; i < n; i++) printf("  %s%s: %d discs, %d files, %s -> %s\n", s[i].base, s[i].loose ? "" : " (folder)",
		                                s[i].n, s[i].nf, s[i].was, s[i].now);
		for (i = 0; i < n; i++) {
			char sh[1024], card[1024];

			snprintf(sh, sizeof sh, "%s/shared", root);
			snprintf(card, sizeof card, "%s/card", root);
			CHECK(ds_apply(roms, sh, card, &s[i]), "%s made", s[i].base);
		}
	}
#define R(f) (snprintf(p, sizeof p, "%s/roms/PlayStation/%s", root, f), p)
	CHECK(!strcmp(slurp(R("Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u")),
	              "Final Fantasy VII (USA) (Disc 1).chd\nFinal Fantasy VII (USA) (Disc 2).chd\n"
	              "Final Fantasy VII (USA) (Disc 3).chd\n"), "FF7's playlist, in order");
	CHECK(access(R("Final Fantasy VII (USA)/Final Fantasy VII (USA) (Disc 3).chd"), F_OK) == 0 &&
	      access(R("Final Fantasy VII (USA) (Disc 3).chd"), F_OK) != 0, "FF7's discs moved into its folder");
	CHECK(access(R("Metal Gear Solid (USA)/Metal Gear Solid (USA) (Disc 2) (Track 2).bin"), F_OK) == 0,
	      "a .cue's tracks went with it");
	CHECK(!strcmp(slurp(R("Metal Gear Solid (USA)/Metal Gear Solid (USA).m3u")),
	              "Metal Gear Solid (USA) (Disc 1).cue\nMetal Gear Solid (USA) (Disc 2).cue\n"), "MGS lists its .cues");
	CHECK(access(R("Chrono Cross/Chrono Cross.m3u"), F_OK) == 0, "a folder of discs gets its playlist");
	CHECK(access(R("Tekken 3 (USA).chd"), F_OK) == 0 && access(R("Spyro (USA) (Disc 1).chd"), F_OK) == 0,
	      "single discs left alone");
	CHECK(!strcmp(slurp(R("Done/Done.m3u")), "Done (Disc 1).chd\nDone (Disc 2).chd\n"), "a playlist there already kept");
	CHECK(!strcmp(slurp(R(".media/Final Fantasy VII (USA).png")), "art"), "box art under the game's name");
#undef R
	snprintf(p, sizeof p, "%s/card/Saves/Final Fantasy VII (USA).srm", root);
	CHECK(!strcmp(slurp(p), "save"), "the save, under the playlist's name");
	snprintf(p, sizeof p, "%s/card/Saves/States/PlayStation/Final Fantasy VII (USA)/Final Fantasy VII (USA).m3u.1.state", root);
	CHECK(!strcmp(slurp(p), "state"), "disc 1's state, under the playlist's");
	{
		char roms[1024];

		snprintf(roms, sizeof roms, "%s/roms", root);
		CHECK(ds_scan(roms, "PlayStation", s, DS_MAX) == 0, "nothing left to do");
	}
	{
		char cmd[1100];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
		if (system(cmd) != 0) {}
	}
	if (fails) { printf("discset-check: %d failed\n", fails); return 1; }
	printf("discset-check: all passed\n");
	return 0;
}
