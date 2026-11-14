/* ChaOS: Game Boy games into their folders (src/gbsort.h), on a card made here */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/gbsort.h"

static int fails;
#define CK(c, ...) do { if (!(c)) { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static char base[64];

static void put(const char *rel, int flag)
{
	char p[600];
	FILE *f;
	unsigned char r[0x200] = { 0 };

	snprintf(p, sizeof p, "%s/%s", base, rel);
	{
		char d[600], *s;
		snprintf(d, sizeof d, "%s", p);
		for (s = d + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(d, 0755); *s = '/'; }
	}
	f = fopen(p, "wb");
	r[0x143] = (unsigned char)(flag < 0 ? 0 : flag);
	fwrite(r, 1, flag < 0 ? 4 : sizeof r, f);
	fclose(f);
}

static int there(const char *rel)
{
	char p[600];
	snprintf(p, sizeof p, "%s/%s", base, rel);
	return access(p, F_OK) == 0;
}

static const gbs_move *find(const gbs_move *m, int n, const char *file)
{
	int i;
	for (i = 0; i < n; i++) if (!strcmp(m[i].file, file)) return &m[i];
	return NULL;
}

int main(void)
{
	static gbs_move m[64];
	char roms[128], shared[128], cache[160], cmd[256];
	const gbs_move *x;
	int n, i;

	snprintf(base, sizeof base, "/tmp/gbsort-XXXXXX");
	if (!mkdtemp(base)) return 1;
	snprintf(roms, sizeof roms, "%s/Roms", base);
	snprintf(shared, sizeof shared, "%s/shared", base);
	snprintf(cache, sizeof cache, "%s/gbsort.cache", base);

	put("Roms/Game Boy/Tetris.gb", 0x00);                 /* right */
	put("Roms/Game Boy/Quest Color.gb", 0x80);            /* GB Studio color: to Color */
	put("Roms/Game Boy/Only.gbc", 0xC0);                  /* Color only: to Color */
	put("Roms/Game Boy Color/Mono.gbc", 0x00);            /* mono named .gbc: to Game Boy */
	put("Roms/Game Boy Color/Zelda DX.gbc", 0x80);        /* right */
	put("Roms/Game Boy Color/Twin.gb", 0x00);             /* to Game Boy, which has a Twin.gb */
	put("Roms/Game Boy/Twin.gb", 0x00);
	put("Roms/Game Boy/Broken.gb", -1);                   /* too short to read: left */
	put("Roms/Game Boy/notes.txt", 0x80);                 /* not a game file */
	/* its things */
	put("Roms/Game Boy/.media/Quest Color.png", 0);
	put("Roms/Game Boy/.media/Quest Color.Extra.png", 0); /* another name's: stays */
	put("shared/.tortos/Game Boy/Quest Color.gb.auto.state", 0);
	put("shared/.tortos/Game Boy/Quest Color.gb.auto.bmp", 0);
	put("shared/.tortos/Game Boy/Quest Color.gb.1.state", 0);
	put("Cheats/Game Boy/Quest Color.cht", 0);

	n = gbs_scan(roms, cache, m, 64);
	CK(n == 4, "four in the other folder, not %d", n);
	CK((x = find(m, n, "Quest Color.gb")) && !strcmp(x->to, GBS_GBC) && !x->clash && !strcmp(x->name, "Quest Color"),
	   "a color .gb goes to Game Boy Color");
	CK((x = find(m, n, "Only.gbc")) && !strcmp(x->to, GBS_GBC), "a Color-only cartridge too");
	CK((x = find(m, n, "Mono.gbc")) && !strcmp(x->to, GBS_GB), "a mono .gbc goes to Game Boy");
	CK((x = find(m, n, "Twin.gb")) && x->clash, "a name the other folder has is a clash");
	CK(!find(m, n, "Tetris.gb") && !find(m, n, "Broken.gb") && !find(m, n, "notes.txt"),
	   "the right ones, the unreadable and the non-games are left");

	x = find(m, n, "Quest Color.gb");
	CK(x && gbs_move_files(roms, shared, base, x), "moved");
	CK(there("Roms/Game Boy Color/Quest Color.gb") && !there("Roms/Game Boy/Quest Color.gb"), "the game");
	CK(there("Roms/Game Boy Color/.media/Quest Color.png") && there("Roms/Game Boy/.media/Quest Color.Extra.png"),
	   "its box art, and not another name's");
	CK(there("shared/.tortos/Game Boy Color/Quest Color.gb.auto.state") &&
	   there("shared/.tortos/Game Boy Color/Quest Color.gb.auto.bmp") &&
	   there("shared/.tortos/Game Boy Color/Quest Color.gb.1.state") &&
	   !there("shared/.tortos/Game Boy/Quest Color.gb.auto.state"), "its states and their pictures");
	CK(there("Cheats/Game Boy Color/Quest Color.cht") && !there("Cheats/Game Boy/Quest Color.cht"), "its cheats");
	x = find(m, n, "Twin.gb");
	CK(x && !gbs_move_files(roms, shared, base, x) && there("Roms/Game Boy Color/Twin.gb"), "a clash isn't moved");

	/* again: the cache is used, and what moved is no longer listed */
	n = gbs_scan(roms, cache, m, 64);
	CK(n == 3 && !find(m, n, "Quest Color.gb"), "three left after one moved, not %d", n);
	for (i = 0; i < n; i++) if (!m[i].clash) gbs_move_files(roms, shared, base, &m[i]);
	n = gbs_scan(roms, cache, m, 64);
	CK(n == 1 && m[0].clash, "only the clash left");
	CK(gbs_scan("/tmp/no-such-gbsort", NULL, m, 64) == -1, "no folders: -1");

	snprintf(cmd, sizeof cmd, "rm -rf %s", base);
	if (system(cmd)) {}
	if (fails) { printf("gbsort: %d failures\n", fails); return 1; }
	printf("ok: Game Boy games found in the other folder and moved with their things\n");
	return 0;
}
