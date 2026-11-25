/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: src/romm.c against a stand-in RomM (tools/romm-mock.py): pairing
 * both ways, the library, downloads' URLs, firmware, and a save sync.
 *
 *   make check-romm      (needs python3 and curl) */
#include "romm.h"
#include "net.h"

#include <signal.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static pid_t start(int port, bool old)
{
	char ps[16];
	pid_t pid = fork();

	snprintf(ps, sizeof ps, "%d", port);
	if (pid == 0) {
		execlp("python3", "python3", "-I", "tools/romm-mock.py", ps, old ? "--old" : NULL, (char *)NULL);
		_exit(127);
	}
	usleep(700 * 1000);
	return pid;
}

static void put(const char *p, const char *s)
{
	FILE *f = fopen(p, "w");

	if (f) { fputs(s, f); fclose(f); }
}

static bool lists(int rom_id, const char *folder, const char *file, void *ctx)
{
	(*(int *)ctx)++;
	(void)rom_id; (void)folder; (void)file;
	return true;
}

/* romm_roms_all's progress: stops once a page has come */
static bool stop_after_one(void *ctx, int have, int total)
{
	(void)total;
	++*(int *)ctx;
	return have == 0;
}

int main(void)
{
	char dir[] = "/tmp/romm-check-XXXXXX", why[160], url[700], p[600], state[8192];
	romm_conn c, again;
	romm_pair pair;
	romm_platform pl[8];
	romm_rom roms[8];
	romm_firmware fw[4];
	int n, total, port = 18000 + getpid() % 1000, k, r, session;
	pid_t srv;

	if (!mkdtemp(dir)) return 1;
	srv = start(port, false);

	/* a server, as typed */
	memset(&c, 0, sizeof c);
	CHECK(!romm_set_url(&c, "  "), "nothing isn't a server");
	snprintf(p, sizeof p, "127.0.0.1:%d/", port);
	CHECK(romm_set_url(&c, p), "set");
	snprintf(p, sizeof p, "http://127.0.0.1:%d", port);
	CHECK(!strcmp(c.url, p), "http:// added, slash gone: %s", c.url);

	/* "/api" typed on the end is RomM's, not the server's address */
	{
		romm_conn t;

		memset(&t, 0, sizeof t);
		CHECK(romm_set_url(&t, "https://romm.example.com/api/") &&
		      !strcmp(t.url, "https://romm.example.com"), "/api dropped: %s", t.url);
		CHECK(romm_set_url(&t, "https://example.com/romm/API") &&
		      !strcmp(t.url, "https://example.com/romm"), "under a path too: %s", t.url);
		CHECK(romm_set_url(&t, "https://api.example.com") &&
		      !strcmp(t.url, "https://api.example.com"), "a host named api kept: %s", t.url);
	}

	/* RomM 5's pairing. RomM 5.4 requires client_device_identifier and
	 * refuses a body without it with 422, as the mock now does; ChaOS left it
	 * out on a first pairing (a user's 422, 2026-11-23). */
	r = romm_pair_begin(&c, "Luke's Brick", &pair, why, sizeof why);
	CHECK(r == 1 && !strcmp(pair.user_code, "WXYZ-1234"), "pairing begun: %d %s", r, why);
	{
		size_t i;
		bool hex = strlen(c.ident) == 6 + 32 && !strncmp(c.ident, "chaos-", 6);

		for (i = 6; hex && c.ident[i]; i++) hex = isxdigit((unsigned char)c.ident[i]) != 0;
		CHECK(hex, "a new Brick names itself at random: \"%s\"", c.ident);
	}
	{
		romm_conn t;
		romm_pair tp;
		char saved[80];

		snprintf(saved, sizeof saved, "%s", c.ident);
		memset(&t, 0, sizeof t);
		snprintf(t.url, sizeof t.url, "%s", c.url);
		snprintf(t.device_id, sizeof t.device_id, "dev-paired-before");
		CHECK(romm_pair_begin(&t, "b", &tp, why, sizeof why) == 1 &&
		      !strcmp(t.ident, "dev-paired-before"),
		      "a Brick paired before keeps the id RomM knows it by: \"%s\"", t.ident);
		memset(&t, 0, sizeof t);
		snprintf(t.url, sizeof t.url, "%s", c.url);
		CHECK(romm_pair_begin(&t, "", &tp, why, sizeof why) == -1 &&
		      strstr(why, "422") && strstr(why, "name: Field required"),
		      "a refusal says what RomM said: \"%s\"", why);
		CHECK(!strcmp(c.ident, saved), "pairing again keeps the name");
	}
	snprintf(p, sizeof p, "http://127.0.0.1:%d/pair/device?user_code=WXYZ-1234", port);
	CHECK(!strcmp(pair.verify, p), "where to approve it: %s", pair.verify);
	CHECK(romm_pair_poll(&c, &pair, why, sizeof why) == ROMM_PAIR_WAIT, "waits first");
	CHECK(romm_pair_poll(&c, &pair, why, sizeof why) == ROMM_PAIR_OK, "then approved: %s", why);
	CHECK(!strcmp(c.token, "rmm_testtoken") && !strcmp(c.device_id, "dev-brick-1"), "token and device");
	CHECK(romm_whoami(&c, why, sizeof why) && !strcmp(c.user, "player"), "who: %s", c.user);
	CHECK(romm_register(&c, "x", why, sizeof why), "already a device: nothing to do");
	CHECK(romm_save(&c, dir), "kept");
	CHECK(romm_load(&again, dir) && !strcmp(again.token, c.token) && !strcmp(again.user, "player"), "read back");
	CHECK(!strcmp(again.ident, c.ident), "the Brick's name for itself read back: %s", again.ident);
	{
		romm_conn t = again;

		romm_forget(&t, dir);
		CHECK(romm_load(&t, dir) == false && !strcmp(t.ident, c.ident),
		      "signing out keeps it, so pairing again finds the same device");
		CHECK(romm_save(&c, dir), "kept again");
	}
	snprintf(p, sizeof p, "%s/token", dir);
	{
		struct stat st;

		CHECK(stat(p, &st) == 0 && (st.st_mode & 077) == 0, "the token is the owner's alone");
	}

	/* the library */
	n = romm_platforms(&c, pl, 8, why, sizeof why);
	CHECK(n == 3, "3 platforms, got %d (%s)", n, why);
	CHECK(n == 3 && pl[0].tag && !strcmp(pl[0].tag, "GB") && pl[1].tag && !strcmp(pl[1].tag, "PCE") && !pl[2].tag,
	      "mapped to ChaOS's systems; PS2 isn't one");
	CHECK(n == 3 && !strcmp(pl[1].name, "PC Engine"), "a custom name shown");
	n = romm_roms(&c, 1, 0, 50, roms, 8, &total, why, sizeof why);
	CHECK(n == 2 && total == 2, "2 Game Boy games: %d/%d %s", n, total, why);
	CHECK(n == 2 && roms[0].file_id == 8 && !strcmp(roms[0].file_name, "Zelda (USA).gb"),
	      "the game's file, not its manual: %d %s", roms[0].file_id, roms[0].file_name);
	CHECK(!strcmp(roms[0].name, "Link's \"Awakening\""), "name unescaped");
	romm_rom_url(&c, &roms[0], url, sizeof url);
	snprintf(p, sizeof p, "http://127.0.0.1:%d/api/roms/1/content/Zelda%%20%%28USA%%29.gb?file_ids=8", port);
	CHECK(!strcmp(url, p), "download URL: %s", url);
	romm_cover_url(&c, &roms[0], url, sizeof url);
	CHECK(strstr(url, "/assets/romm/resources/roms/1/1/cover/small.png") == url + strlen(c.url), "cover %s", url);
	romm_cover_url(&c, &roms[1], url, sizeof url);
	CHECK(!strcmp(url, "https://images.igdb.com/t.jpg"), "an outside cover as it is");
	CHECK(roms[0].nplay == 1, "a game and its manual: no choice to ask (%d)", roms[0].nplay);
	n = romm_roms(&c, 2, 0, 50, roms, 8, &total, why, sizeof why);
	CHECK(n == 1 && roms[0].multi, "a multi-file game");
	CHECK(n == 1 && roms[0].nplay == 2 && !roms[0].file_id, "its two files counted, none chosen: %d", roms[0].nplay);
	{
		/* the file picker's list (ChaOS 2.7.8): the game's files, not its manual */
		romm_file fl[ROMM_FILES_MAX];
		int nf = romm_rom_files(&c, 3, fl, ROMM_FILES_MAX, why, sizeof why);

		CHECK(nf == 2 && fl[0].id == 10 && !strcmp(fl[1].name, "Ys.bin") && fl[1].size == 1536,
		      "a game's files: %d %s", nf, why);
		nf = romm_rom_files(&c, 1, fl, ROMM_FILES_MAX, why, sizeof why);
		CHECK(nf == 1 && fl[0].id == 8, "the manual left out: %d", nf);
		CHECK(romm_rom_files(&c, 999, fl, ROMM_FILES_MAX, why, sizeof why) < 0, "an unknown game: said");
	}
	romm_rom_url(&c, &roms[0], url, sizeof url);
	CHECK(strstr(url, "/api/roms/3/content/Ys%20I%20%26%20II") && !strchr(url, '?'), "its zip: %s", url);
	{
		/* the real thing: downloaded with the token */
		net_req q;
		char out[64];
		int http;

		memset(&q, 0, sizeof q);
		snprintf(url, sizeof url, "http://127.0.0.1:%d/api/roms/1/content/x?file_ids=8", port);
		q.url = url; q.bearer = c.token; q.timeout_s = 10;
		CHECK(net_request(&q, out, sizeof out, &http) == 12 && http == 200 && !strcmp(out, "ZELDA-ROM-12"),
		      "a download with the token: %d %s", http, out);
		q.bearer = "wrong";
		net_request(&q, out, sizeof out, &http);
		CHECK(http == 401, "and without it, refused: %d", http);
	}
	n = romm_firmware_list(&c, 1, fw, 4, why, sizeof why);
	CHECK(n == 1 && !strcmp(fw[0].file_name, "gb_bios.bin"), "firmware, a path-y name refused: %d", n);
	romm_firmware_url(&c, &fw[0], url, sizeof url);
	CHECK(strstr(url, "/api/firmware/5/content/gb_bios.bin") != NULL, "firmware URL %s", url);

	/* The whole list, paged (ChaOS 2.7.8). A PlayStation library's pages,
	 * each game listing every disc's tracks, passed the 1 MB reply: the page
	 * was cut off, its total with it, and the list ended at 42 of 282 with
	 * nothing said. */
	{
		romm_rom *all = NULL;
		int got = 0, r, seen = 0;
		bool dup = false;

		r = romm_roms(&c, 4, 0, 100, roms, 8, &total, why, sizeof why);
		CHECK(r == ROMM_PAGE_TOO_BIG, "a page too long to read is said, not half-read: %d", r);
		r = romm_roms_all(&c, 4, 282, &all, &got, NULL, NULL, why, sizeof why);
		CHECK(r == 0 && got == 282, "all 282 big games, in smaller pages: %d %d %s", r, got, why);
		for (int i = 1; i < got; i++) dup |= all[i].id == all[i - 1].id;
		CHECK(got == 282 && !dup && all[0].id == 1000 && all[281].id == 1281, "each once, in order");
		free(all);
		all = NULL;
		r = romm_roms_all(&c, 5, 75, &all, &got, NULL, NULL, why, sizeof why);
		CHECK(r == 0 && got == 75 && all && all[74].id == 5074, "a server's smaller pages followed: %d %d", r, got);
		free(all);
		all = NULL;
		r = romm_roms_all(&c, 6, 0, &all, &got, NULL, NULL, why, sizeof why);
		CHECK(r == 0 && got == 130, "no total: on to a short page: %d %d", r, got);
		free(all);
		all = NULL;
		r = romm_roms_all(&c, 1, 2, &all, &got, NULL, NULL, why, sizeof why);
		CHECK(r == 0 && got == 2, "a small platform, one page: %d %d", r, got);
		free(all);
		all = NULL;
		r = romm_roms_all(&c, 4, 282, &all, &got, stop_after_one, &seen, why, sizeof why);
		CHECK(r == ROMM_LIST_PARTIAL && seen >= 2 && got > 0 && got < 282, "stopped by the player: %d %d", r, got);
		free(all);
	}

	/* Games put on the card without RomM, matched by name so their saves
	 * sync (ChaOS 2.7.8): sync saw only games RomM had downloaded. */
	{
		romm_rom g[4];

		memset(g, 0, sizeof g);
		g[0].id = 1; snprintf(g[0].fs_name, ROMM_STR, "Zelda (USA).gb"); snprintf(g[0].file_name, ROMM_STR, "Zelda (USA).gb");
		g[1].id = 2; snprintf(g[1].fs_name, ROMM_STR, "Tetris.gb"); snprintf(g[1].file_name, ROMM_STR, "Tetris.gb");
		g[2].id = 3; g[2].multi = true; snprintf(g[2].fs_name, ROMM_STR, "Ys I & II");
		snprintf(g[2].file_name, ROMM_STR, "Ys.cue");
		g[3].id = 4; snprintf(g[3].fs_name, ROMM_STR, "tetris.GB"); snprintf(g[3].file_name, ROMM_STR, "tetris.GB");
		CHECK(romm_match_file(g, 4, "Zelda (USA).gb") == 0, "RomM's own name");
		CHECK(romm_match_file(g, 4, "Ys I & II") == 2, "a multi-file game's folder");
		CHECK(romm_match_file(g, 4, "zelda_usa.GBC") == 0, "the same name, loosely");
		CHECK(romm_match_file(g, 4, "Zelda (Europe).gb") == -1, "another region isn't the same game");
		CHECK(romm_match_file(g, 4, "TETRIS.gb") == -1, "two games answer: neither taken");
		CHECK(romm_match_file(g, 4, "Tetris.gb") == 1, "but the exact name still is");
		CHECK(romm_match_file(g, 4, "...") == -1 && romm_match_file(g, 0, "Tetris.gb") == -1, "nothing to go on");
	}

	/* games.tsv */
	CHECK(romm_map_add(dir, 1, "Game Boy", "Zelda (USA).gb"), "mapped");
	CHECK(romm_map_add(dir, 2, "Game Boy", "Tetris.gb"), "mapped");
	CHECK(romm_map_add(dir, 1, "Game Boy", "Zelda (USA).gb"), "again: no second line");
	CHECK(romm_map_find(dir, "Game Boy", "Tetris.gb") == 2 && !romm_map_find(dir, "NES", "Tetris.gb"), "found");
	k = 0;
	romm_map_each(dir, lists, &k);
	CHECK(k == 2, "two games, once each: %d", k);

	/* a sync */
	{
		romm_local_save s[1];
		romm_sync_op ops[8];
		char save[600];

		memset(s, 0, sizeof s);
		s[0].rom_id = 1;
		snprintf(s[0].file_name, sizeof s[0].file_name, "Zelda (USA).srm");
		snprintf(s[0].md5, sizeof s[0].md5, "0123456789abcdef0123456789abcdef");
		s[0].mtime = 1700000000;
		s[0].size = 10;
		n = romm_negotiate(&c, s, 1, &session, ops, 8, why, sizeof why);
		CHECK(n == 3 && session == 77, "the plan: %d ops, session %d (%s)", n, session, why);
		CHECK(n == 3 && !strcmp(ops[0].action, "upload") && !strcmp(ops[1].action, "download") &&
		      ops[1].save_id == 41, "upload one, download one");
		snprintf(save, sizeof save, "%s/Zelda (USA).srm", dir);
		put(save, "LOCAL-SAVE");
		CHECK(romm_save_upload(&c, 1, save, why, sizeof why), "uploaded: %s", why);
		snprintf(save, sizeof save, "%s/Tetris.srm", dir);
		CHECK(romm_save_download(&c, 41, save, why, sizeof why), "downloaded: %s", why);
		{
			FILE *f = fopen(save, "r");
			char b[32] = "";

			if (f) { if (!fgets(b, sizeof b, f)) b[0] = '\0'; fclose(f); }
			CHECK(!strcmp(b, "SERVER-SAVE"), "the server's save: %s", b);
		}
		CHECK(romm_sync_complete(&c, session, 2, 0), "session completed");
	}
	printf("save states (ChaOS 2.7.3)\n");
	{
		romm_state st[8];
		char sp[512], got[64] = "";
		int n, k, mine = 0;
		FILE *f;

		n = romm_states(&c, 2, st, 8, why, sizeof why);
		CHECK(n == 1 && st[0].id == 60 && !strcmp(st[0].file_name, "Tetris.gb.1.state") &&
		      st[0].updated == 1795168800 && st[0].size == 12, "the server's one: %d", n);
		snprintf(sp, sizeof sp, "%s/Tetris.gb.1.state", dir);
		CHECK(romm_state_download(&c, 60, sp, why, sizeof why), "down: %s", why);
		if ((f = fopen(sp, "r"))) { if (!fgets(got, sizeof got, f)) got[0] = 0; fclose(f); }
		CHECK(!strcmp(got, "SERVER-STATE"), "its content: %s", got);
		snprintf(sp, sizeof sp, "%s/Tetris.gb.auto.state", dir);
		if ((f = fopen(sp, "w"))) { fputs("LOCAL-STATE", f); fclose(f); }
		CHECK(romm_state_upload(&c, 2, 0, sp, why, sizeof why), "up, new: %s", why);
		n = romm_states(&c, 2, st, 8, why, sizeof why);
		for (k = 0; k < n; k++) if (!strcmp(st[k].file_name, "Tetris.gb.auto.state")) mine = st[k].id;
		CHECK(n == 2 && mine == 61, "listed with its name: %d, %d", n, mine);
		if ((f = fopen(sp, "w"))) { fputs("NEWER-STATE", f); fclose(f); }
		CHECK(romm_state_upload(&c, 2, 61, sp, why, sizeof why), "up, over it: %s", why);
		snprintf(sp, sizeof sp, "%s/back.state", dir);
		CHECK(romm_state_download(&c, 61, sp, why, sizeof why), "and back: %s", why);
		got[0] = 0;
		if ((f = fopen(sp, "r"))) { if (!fgets(got, sizeof got, f)) got[0] = 0; fclose(f); }
		CHECK(!strcmp(got, "NEWER-STATE"), "replaced, not added: %s", got);
		CHECK(romm_states(&c, 2, st, 8, why, sizeof why) == 2, "still two");
	}
	{
		net_req q;
		int http;

		memset(&q, 0, sizeof q);
		snprintf(url, sizeof url, "http://127.0.0.1:%d/__state", port);
		q.url = url; q.method = "POST"; q.json = "{}"; q.timeout_s = 5;
		net_request(&q, state, sizeof state, &http);
		CHECK(strstr(state, "\"rom_id\": \"1\"") && strstr(state, "\"device_id\": \"dev-brick-1\"") &&
		      strstr(state, "\"ok\": true") && strstr(state, "\"has\": true"), "the upload as RomM wants it");
		CHECK(strstr(state, "\"downloaded\": [41]") != NULL, "the download marked");
		CHECK(strstr(state, "\"operations_completed\": 2") != NULL, "the counts sent");
		CHECK(strstr(state, "\"content_hash\": \"0123456789abcdef0123456789abcdef\"") &&
		      strstr(state, "\"updated_at\": \"2023-11-14T22:13:20Z\""), "negotiate's body");
	}
	romm_forget(&c, dir);
	CHECK(!romm_load(&again, dir) && again.url[0] && !again.token[0], "signed out, the server kept");
	kill(srv, SIGTERM);
	waitpid(srv, NULL, 0);

	/* RomM 4: no device pairing, a pairing code */
	srv = start(port + 1, true);
	memset(&c, 0, sizeof c);
	snprintf(p, sizeof p, "http://127.0.0.1:%d", port + 1);
	romm_set_url(&c, p);
	CHECK(romm_pair_begin(&c, "b", &pair, why, sizeof why) == 0, "RomM 4: no pairing flow");
	CHECK(!romm_exchange(&c, "NOPE", why, sizeof why) && strstr(why, "isn't right"), "a wrong code: %s", why);
	CHECK(romm_exchange(&c, "ABCD1234", why, sizeof why) && !strcmp(c.token, "rmm_testtoken"), "the code");
	CHECK(romm_register(&c, "Luke's Brick", why, sizeof why) && !strcmp(c.device_id, "dev-brick-2"),
	      "registered for sync: %s", c.device_id);
	kill(srv, SIGTERM);
	waitpid(srv, NULL, 0);

	/* nothing there at all */
	memset(&c, 0, sizeof c);
	romm_set_url(&c, "http://127.0.0.1:1");
	CHECK(romm_pair_begin(&c, "b", &pair, why, sizeof why) == -1 && strstr(why, "reach"), "no server: %s", why);
	CHECK(!romm_set_url(&c, "has space.local"), "not an address");

	snprintf(p, sizeof p, "rm -rf %s", dir);
	if (system(p) != 0) fails++;
	if (fails) { printf("romm: %d failed\n", fails); return 1; }
	printf("romm: ok\n");
	return 0;
}
