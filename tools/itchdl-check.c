/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: itch.io's download pages read and a game's file chosen, from pages
 * in itch.io's format (tools/fixtures/itch). No network. See src/itchdl.h. */
#include "../src/itchdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *slurp(const char *path)
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
	return b;
}

int main(void)
{
	itchdl_upload u[ITCHDL_UPLOADS];
	char *page = slurp("tools/fixtures/itch/pak-download_page.html");
	char *game = slurp("tools/fixtures/itch/pak-game_page_free.html");
	char tok[160], id[32];
	bool several;
	int n, k;

	printf("a game's page: its token\n");
	CHECK(game && itchdl_csrf(game, tok, sizeof tok) && !strcmp(tok, "ZmFrZS1jc3JmLXRva2Vu"),
	      "csrf from <meta name=csrf_token value=>: \"%s\"", game ? tok : "(no fixture)");
	CHECK(itchdl_csrf("<input type=\"hidden\" name=\"csrf_token\" content=\"abc\"/>", tok, sizeof tok) &&
	      !strcmp(tok, "abc"), "csrf from content=");
	CHECK(!itchdl_csrf("<p>nothing</p>", tok, sizeof tok), "no csrf where there is none");

	printf("a download page: its files\n");
	n = page ? itchdl_parse_page(page, u, ITCHDL_UPLOADS) : -1;
	CHECK(n == 3, "three files, got %d", n);
	CHECK(n == 3 && !strcmp(u[0].id, "11111") && !strcmp(u[0].name, "game.gbc"), "first: game.gbc");
	CHECK(n == 3 && !strcmp(u[1].name, "manual.pdf") && !strcmp(u[2].name, "game.gb"), "then manual.pdf, game.gb");
	k = itchdl_parse_page("<div class=\"upload has_meta\"><strong title=\"Tom &amp; Jerry.gb\" "
	                      "class=\"name\">x</strong><a data-upload_id=\"77\" class=\"button\">Download</a></div>"
	                      "<div class=\"upload\"><strong class=\"name\">  other.nes  </strong>"
	                      "<a class=\"button\" data-upload_id=\"78\">Download</a></div>", u, ITCHDL_UPLOADS);
	CHECK(k == 2 && !strcmp(u[0].name, "Tom & Jerry.gb") && !strcmp(u[1].name, "other.nes") &&
	      !strcmp(u[1].id, "78"), "other classes, attributes either way round, a name in the text");

	printf("a page itch.io served: its files, the button first\n");
	{
		char *live = slurp("tools/fixtures/itch/pak-game_page_live_free.html");
		int order[ITCHDL_UPLOADS];

		k = live ? itchdl_parse_page(live, u, ITCHDL_UPLOADS) : -1;
		CHECK(k == 4, "four files, got %d", k);
		CHECK(k == 4 && !strcmp(u[0].id, "19334755") && !strcmp(u[0].name, "web_1.3.zip") &&
		      !strcmp(u[3].id, "19334859") && !strcmp(u[3].name, "Criss Cross Cove (GB Rom)"),
		      "ids and labels kept together");
		k = k == 4 ? itchdl_rank(u, 4, "gb,dmg,gbc,cgb,zip", order, &several) : 0;
		CHECK(k >= 1 && order[0] == 3 && !several, "Game Boy: the GB Rom, not the web build, map or Pocket file");
		free(live);
	}

	printf("choosing a console's file\n");
	{
		itchdl_upload a[] = { { "1", "manual.pdf", 0 }, { "2", "game.zip", 0 }, { "3", "game.gbc", 0 },
		                      { "4", "game.gb", 0 } };
		const char *gb = "gb,dmg,gbc,cgb,zip", *gbc = "gbc,cgb,gb,dmg,zip", *nes = "nes,fds,unf,unif,zip";
		int order[ITCHDL_UPLOADS];

		CHECK(itchdl_rank(a, 4, gb, order, &several) == 3 && order[0] == 2 && !several,
		      "Game Boy takes the .gbc first, as the pak does: the Color version");
		CHECK(itchdl_rank(a, 4, gbc, order, &several) == 3 && order[0] == 2 && !several, "Game Boy Color takes the .gbc");
		CHECK(itchdl_rank(a, 4, nes, order, &several) == 1 && order[0] == 1, "NES takes the zip, the ROMs being others'");
		CHECK(itchdl_rank(a, 1, nes, order, &several) == 0, "a manual alone is nothing");
		{
			itchdl_upload p[] = { { "1", "screenshot.png", 0 }, { "2", "celeste.p8.png", 0 },
			                      { "3", "celeste.p8", 0 } };

			CHECK(itchdl_rank(p, 3, "p8,png", order, &several) == 2 && order[0] == 1 && order[1] == 2,
			      "PICO-8: a .p8.png cart, then a .p8, as the pak chooses; never a picture");
			CHECK(itchdl_ext_in("cart.p8.png", "p8,png", false) && !itchdl_ext_in("shot.png", "p8,png", false),
			      "and a .png counts only as a .p8.png");
		}
		{
			itchdl_upload b[] = { { "1", "demo.nes", 0 }, { "2", "full.NES", 0 } };

			CHECK(itchdl_rank(b, 2, nes, order, &several) == 2 && order[0] == 1 && !several,
			      "a demo after the game");
		}
		{
			itchdl_upload b[] = { { "1", "Game (EN)", 0 }, { "2", "Game (FR)", 0 }, { "3", "Game v1.2", 0 } };

			CHECK(itchdl_rank(b, 3, nes, order, &several) == 3 && several, "labels alike: ask");
		}
		CHECK(itchdl_ext_in("x.GBA", "gba,agb,zip", false) && !itchdl_ext_in("x.zip", "gba,agb,zip", false) &&
		      itchdl_ext_in("x.zip", "gba,agb,zip", true), "extensions, any case, zip when asked");
	}

	printf("what a file is, by its bytes\n");
	{
		static unsigned char rom[0x8000];
		char fo[32], ex[8];
		const char *t = "/tmp/itchdl-check.rom";
		FILE *f;
		static const unsigned char logo[8] = { 0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B };

#define WRITE() do { f = fopen(t, "wb"); fwrite(rom, 1, sizeof rom, f); fclose(f); } while (0)
		memset(rom, 0, sizeof rom); memcpy(rom + 0x104, logo, 8); WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(fo, "Game Boy") && !strcmp(ex, "gb"), "Game Boy");
		rom[0x143] = 0xC0; WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(fo, "Game Boy Color"), "Game Boy Color only");
		rom[0x143] = 0x80; WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(ex, "gbc"), "Game Boy Color, both");
		memset(rom, 0, sizeof rom); memcpy(rom, "NES\x1A", 4); WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(fo, "NES"), "NES");
		memset(rom, 0, sizeof rom); memcpy(rom + 0x100, "SEGA GENESIS", 12); WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(fo, "Genesis"), "Genesis");
		memset(rom, 0, sizeof rom); memcpy(rom + 0x7FF0, "TMR SEGA", 8); rom[0x7FFF] = 0x4C; WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(fo, "Master System"), "Master System");
		rom[0x7FFF] = 0x6C; WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(fo, "Game Gear"), "Game Gear");
		memset(rom, 0, sizeof rom); memcpy(rom, "pico-8 cartridge // http://www.pico-8.com", 41); WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(fo, "PICO-8") && !strcmp(ex, "p8"),
		      "a PICO-8 text cart");
		memset(rom, 0, sizeof rom); memcpy(rom, "\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\0\x80\0\0\0\xa0", 24); WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(fo, "PICO-8") && !strcmp(ex, "p8.png"),
		      "a PICO-8 picture cart: a PNG 128 wide");
		rom[19] = 0x81; WRITE();
		CHECK(!itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex), "and any other picture: nothing");
		memset(rom, 0, sizeof rom); memcpy(rom, "PK\x03\x04", 4); WRITE();
		CHECK(itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex) && !strcmp(ex, "zip"), "a zip");
		memset(rom, 0, sizeof rom); WRITE();
		CHECK(!itchdl_sniff(t, fo, sizeof fo, ex, sizeof ex), "nothing known");
		unlink(t);
	}

	printf("a signed address: its download key's id\n");
	/* {"id":123456789,"game_id":5033642} as base64, then a signature */
	CHECK(itchdl_key_id("https://leafthief.itch.io/crisscrosscove/download/eyJpZCI6MTIzNDU2Nzg5LCJnYW1lX2lkIjo1MDMzNjQyfQ%3D%3D.abc123",
	                    id, sizeof id) && !strcmp(id, "123456789"), "id from the key: \"%s\"", id);
	CHECK(itchdl_key_id("https://a.itch.io/g/download/eyJpZCI6NDJ9.sig?after=1", id, sizeof id) &&
	      !strcmp(id, "42"), "unpadded, with a query after");
	CHECK(!itchdl_key_id("https://a.itch.io/g/download/notakey", id, sizeof id), "no id in nonsense");

	printf("the key, kept\n");
	{
		char dir[] = "/tmp/itchdl-check-XXXXXX", key[128];

		CHECK(mkdtemp(dir) != NULL, "a folder");
		CHECK(!itchdl_key_load(dir, key, sizeof key), "none at first");
		CHECK(itchdl_key_save(dir, "abcDEF123") && itchdl_key_load(dir, key, sizeof key) &&
		      !strcmp(key, "abcDEF123"), "saved and read back");
		CHECK(itchdl_key_save(dir, "") && !itchdl_key_load(dir, key, sizeof key), "removed");
		rmdir(dir);
	}

	free(page);
	free(game);
	if (fails) { printf("itchdl-check: %d failed\n", fails); return 1; }
	printf("itchdl-check: all passed\n");
	return 0;
}
