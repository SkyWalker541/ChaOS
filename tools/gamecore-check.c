/* ChaOS: one game's core options (src/gamecore.h) - the machine from the
 * header, read from files and from zips made here, and PT's settings */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gamecore.h"

static int fails;
#define CK(c, ...) do { if (!(c)) { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void rom(const char *path, int flag)
{
	unsigned char r[0x8000];
	FILE *f = fopen(path, "wb");
	int i;

	for (i = 0; i < (int)sizeof r; i++) r[i] = (unsigned char)(i * 7);
	r[0x143] = (unsigned char)flag;
	fwrite(r, 1, sizeof r, f);
	fclose(f);
}

static int has(char kv[GC_MAX][GC_LEN], int n, const char *want)
{
	int i;
	for (i = 0; i < n; i++) if (!strcmp(kv[i], want)) return 1;
	return 0;
}

int main(void)
{
	char d[] = "/tmp/gamecore-XXXXXX", p[256], cmd[1024], kv[GC_MAX][GC_LEN];
	int n;

	if (!mkdtemp(d)) return 1;
	snprintf(p, sizeof p, "%s/color.gb", d); rom(p, 0x80);
	snprintf(p, sizeof p, "%s/only.gbc", d); rom(p, 0xC0);
	snprintf(p, sizeof p, "%s/mono.gbc", d); rom(p, 0x00);
	snprintf(p, sizeof p, "%s/sgb.gb", d); rom(p, 0x00);
	snprintf(cmd, sizeof cmd,
	         "cd %s && python3 -c \"import zipfile as z;"
	         "a=z.ZipFile('def.zip','w',z.ZIP_DEFLATED);a.writestr('readme.txt','x'*5000);a.write('color.gb','Game (USA).gb');a.close();"
	         "b=z.ZipFile('sto.zip','w',z.ZIP_STORED);b.write('mono.gbc','m.gbc');b.close()\"", d);
	if (system(cmd)) { printf("no python\n"); return 1; }

	snprintf(p, sizeof p, "%s/color.gb", d);
	CK(gc_cgb_flag(p) == 0x80, "flag from a file");
	n = gc_options("GBC", p, -1, kv);
	CK(has(kv, n, "mgba_gb_model=Game Boy Color"), "a color .gb plays in color in Game Boy Color");
	n = gc_options("GB", p, -1, kv);
	CK(has(kv, n, "mgba_gb_model=Game Boy Color"), "and in Game Boy too: it is meant to be color");
	snprintf(p, sizeof p, "%s/only.gbc", d);
	n = gc_options("GB", p, -1, kv);
	CK(has(kv, n, "mgba_gb_model=Game Boy Color"), "a Color-only cartridge never meets its refusal screen");
	snprintf(p, sizeof p, "%s/mono.gbc", d);
	n = gc_options("GBC", p, -1, kv);
	CK(has(kv, n, "mgba_gb_model=Game Boy"), "a mono game named .gbc in Game Boy Color stays a Game Boy");
	snprintf(p, sizeof p, "%s/def.zip", d);
	CK(gc_cgb_flag(p) == 0x80, "flag from inside a deflated zip, past a file before it: %d", gc_cgb_flag(p));
	snprintf(p, sizeof p, "%s/sto.zip", d);
	CK(gc_cgb_flag(p) == 0x00, "flag from a stored zip: %d", gc_cgb_flag(p));
	snprintf(p, sizeof p, "%s/none.gb", d);
	n = gc_options("GB", p, -1, kv);
	CK(gc_cgb_flag(p) == -1 && !has(kv, n, "mgba_gb_model=Game Boy") && !has(kv, n, "mgba_gb_model=Game Boy Color"),
	   "unreadable: the folder's own model stands");

	/* PT */
	snprintf(p, sizeof p, "%s/sgb.gb", d);
	n = gc_options("GB", p, 1, kv);
	CK(has(kv, n, "mgba_color_correction=OFF") && has(kv, n, "mgba_interframe_blending=OFF") &&
	   has(kv, n, "mgba_gb_colors=Grayscale"), "PT Game Boy: no correction, no blending, no colorization");
	n = gc_options("GBC", p, 2, kv);
	CK(has(kv, n, "mgba_color_correction=GBC") && has(kv, n, "mgba_interframe_blending=OFF"), "PT Game Boy Color");
	n = gc_options("GBA", "x.gba", 3, kv);
	CK(n == 2 && has(kv, n, "mgba_color_correction=GBA") && has(kv, n, "mgba_interframe_blending=mix_smart"), "PT GBA SP");
	n = gc_options("GBA", "x.gba", 4, kv);
	CK(has(kv, n, "mgba_color_correction=GBA") && has(kv, n, "mgba_interframe_blending=lcd_ghosting"), "PT GBA");
	n = gc_options("GBA", "x.gba", 0, kv);
	CK(n == 0, "PT Manual sets nothing");
	n = gc_options("GBA", "x.gba", -1, kv);
	CK(has(kv, n, "mgba_color_correction=OFF") && has(kv, n, "mgba_interframe_blending=OFF"), "not PT: mGBA's defaults");
	n = gc_options("NES", "x.nes", 1, kv);
	CK(n == 0, "nothing for other consoles");

	snprintf(cmd, sizeof cmd, "rm -rf %s", d);
	if (system(cmd)) {}
	if (fails) { printf("gamecore: %d failures\n", fails); return 1; }
	printf("ok: each game's machine from its header, and PT's core settings\n");
	return 0;
}
