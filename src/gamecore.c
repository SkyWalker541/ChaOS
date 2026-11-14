/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: one game's core options - see gamecore.h */
#include "gamecore.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

static int ends(const char *s, const char *t)
{
	size_t a = strlen(s), b = strlen(t);
	return a >= b && !strcasecmp(s + a - b, t);
}

static int gb_name(const char *n)
{
	return ends(n, ".gb") || ends(n, ".gbc") || ends(n, ".cgb") || ends(n, ".dmg");
}

static unsigned le16(const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* The first Game Boy file in a zip, by its local headers, as far as 0x144 */
static int zip_flag(FILE *f)
{
	unsigned char h[30], out[0x144], in[4096];
	char name[256];

	while (fread(h, 1, 30, f) == 30 && le32(h) == 0x04034b50) {
		unsigned method = le16(h + 8), flags = le16(h + 6);
		uint32_t csize = le32(h + 18);
		unsigned nlen = le16(h + 26), xlen = le16(h + 28);
		size_t k = nlen < sizeof name ? nlen : sizeof name - 1;

		if (fread(name, 1, k, f) != k) return -1;
		name[k] = '\0';
		if (fseek(f, (long)(nlen - k) + xlen, SEEK_CUR)) return -1;
		if (!gb_name(name)) {
			/* a size known up front, or no walking on */
			if (flags & 8) return -1;
			if (fseek(f, (long)csize, SEEK_CUR)) return -1;
			continue;
		}
		if (method == 0)
			return fread(out, 1, sizeof out, f) == sizeof out ? out[0x143] : -1;
		if (method == 8) {
			z_stream z;
			int r = Z_OK;

			memset(&z, 0, sizeof z);
			if (inflateInit2(&z, -15) != Z_OK) return -1;
			z.next_out = out;
			z.avail_out = sizeof out;
			while (z.avail_out && r == Z_OK) {
				size_t got = fread(in, 1, sizeof in, f);
				if (!got) break;
				z.next_in = in;
				z.avail_in = (uInt)got;
				r = inflate(&z, Z_NO_FLUSH);
			}
			inflateEnd(&z);
			return z.avail_out == 0 ? out[0x143] : -1;
		}
		return -1;
	}
	return -1;
}

int gc_cgb_flag(const char *rom)
{
	FILE *f;
	int v = -1;

	if (!rom || !(f = fopen(rom, "rb"))) return -1;
	if (ends(rom, ".zip")) v = zip_flag(f);
	else {
		unsigned char b;
		if (fseek(f, 0x143, SEEK_SET) == 0 && fread(&b, 1, 1, f) == 1) v = b;
	}
	fclose(f);
	return v;
}

int gc_options(const char *tag, const char *rom, int pt_system, char out[GC_MAX][GC_LEN])
{
	int n = 0;
	int gb = tag && (!strcmp(tag, "GB") || !strcmp(tag, "GBC"));
	int gba = tag && !strcmp(tag, "GBA");

#define PUT(...) do { if (n < GC_MAX) snprintf(out[n++], GC_LEN, __VA_ARGS__); } while (0)
	if (gb) {
		int flag = gc_cgb_flag(rom);

		if (flag >= 0)
			PUT("mgba_gb_model=%s", flag & 0x80 ? "Game Boy Color" : "Game Boy");
	}
	if (!gb && !gba) return n;
	switch (pt_system) {
	case 1:
		PUT("mgba_color_correction=OFF");
		PUT("mgba_interframe_blending=OFF");
		PUT("mgba_gb_colors=Grayscale");
		break;
	case 2: PUT("mgba_color_correction=GBC"); PUT("mgba_interframe_blending=OFF"); break;
	case 3: PUT("mgba_color_correction=GBA"); PUT("mgba_interframe_blending=mix_smart"); break;
	case 4: PUT("mgba_color_correction=GBA"); PUT("mgba_interframe_blending=lcd_ghosting"); break;
	case 0: break;
	default:
		/* not PT: mGBA's own defaults, said, so a change in a game undoes */
		PUT("mgba_color_correction=OFF");
		PUT("mgba_interframe_blending=OFF");
		break;
	}
#undef PUT
	return n;
}
