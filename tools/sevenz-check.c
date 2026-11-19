/* ChaOS: lib/7z's sevenz_largest against small archives in each style 7-Zip
 * makes - LZMA2 with two files in one block, LZMA, and a BCJ filter - the
 * game in each the same 8000 bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "sevenz.h"

static int failures;
#define CHECK(c, ...) do { if (c) { printf("ok: "); } else { printf("FAIL: "); failures++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static bool by_ext(const char *name, void *ext)
{
	const char *dot = strrchr(name, '.');

	return dot && !strcmp(dot, (const char *)ext);
}

static unsigned long sum(const unsigned char *d, size_t n)
{
	unsigned long h = 5381;

	while (n--) h = h * 33 + *d++;
	return h;
}

int main(void)
{
	static const char *const kinds[] = { "lzma2", "lzma", "bcj", NULL };
	unsigned long want = 0;
	unsigned want_crc = 0;
	int k;

	printf("sevenz: the largest file of a .7z\n");
	CHECK(sevenz_is("Game (USA).7z") && sevenz_is("x.7Z") && !sevenz_is("x.zip") && !sevenz_is("7z"),
	      "a .7z is told by its name");
	for (k = 0; kinds[k]; k++) {
		char path[256], name[256];
		unsigned char *d = NULL;
		size_t n = 0;

		snprintf(path, sizeof path, "tools/fixtures/7z/%s.7z", kinds[k]);
		CHECK(sevenz_largest(path, 0, &d, &n, name, sizeof name), "%s: read", kinds[k]);
		if (!d) continue;
		if (!want) { want = sum(d, n); want_crc = (unsigned)crc32(0, d, (unsigned)n); }
		CHECK(n == 8000 && sum(d, n) == want, "%s: the game, whole (%zu bytes)", kinds[k], n);
		CHECK(!strcmp(name, "Metroid (USA).gba"), "%s: its name (%s)", kinds[k], name);
		free(d);
	}
	{
		unsigned crc = 0;

		/* the game's CRC32 from the index; the readme beside it isn't taken */
		CHECK(sevenz_crc("tools/fixtures/7z/lzma2.7z", by_ext, (void *)".gba", &crc) && crc == want_crc,
		      "the game's CRC from the index (%08x)", crc);
		CHECK(sevenz_crc("tools/fixtures/7z/lzma.7z", by_ext, (void *)".nes", &crc) && crc == want_crc,
		      "the only file's CRC when none is the right kind");
		CHECK(!sevenz_crc("tools/fixtures/7z/lzma2.7z", by_ext, (void *)".nes", &crc),
		      "two files and neither the right kind: no CRC");
	}
	{
		unsigned char *d = (unsigned char *)1;
		size_t n = 1;

		CHECK(!sevenz_largest("tools/fixtures/7z/lzma2.7z", 100, &d, &n, NULL, 0) && !d && !n,
		      "a game over the limit is refused, nothing kept");
		CHECK(!sevenz_largest("tools/fixtures/7z/none.7z", 0, &d, &n, NULL, 0) && !d,
		      "an archive that isn't there is refused");
		CHECK(!sevenz_largest("tools/sevenz-check.c", 0, &d, &n, NULL, 0) && !d,
		      "a file that isn't a .7z is refused");
	}
	printf(failures ? "FAILED\n" : "all good\n");
	return failures ? 1 : 0;
}
