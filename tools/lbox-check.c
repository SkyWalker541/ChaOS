/* ChaOS: lbox.c's readers against LaunchBox's pages as they were on
 * 2026-11-19 (tools/fixtures/lbox). No network. */
#include "lbox.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* lbox_search/lbox_images aren't run here: net.c isn't linked */
long net_request(const net_req *r, char *out, size_t outn, int *http) { (void)r; (void)out; (void)outn; *http = 0; return -1; }
void net_urlencode(const char *in, char *out, size_t outn) { snprintf(out, outn, "%s", in); }

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } } while (0)

static char *slurp(const char *p)
{
	FILE *f = fopen(p, "rb");
	long n;
	char *b;
	if (!f) { perror(p); exit(2); }
	fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
	b = malloc((size_t)n + 1);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(2);
	b[n] = 0; fclose(f);
	return b;
}

int main(void)
{
	lbox_game g[10];
	lbox_image im[40];
	char *h = slurp("tools/fixtures/lbox/results.html");
	int n = lbox_parse_results(h, g, 10), i;

	CHECK(n == 3, "three games");
	CHECK(g[0].id == 140732 && !strcmp(g[0].title, "Pok\xc3\xa9mon Chaos 2"), "first: id, and its title decoded");
	CHECK(!strcmp(g[0].platform, "Nintendo Game Boy Advance"), "first: platform");
	CHECK(g[1].id == 2243 && !strcmp(g[1].title, "Pok\xc3\xa9mon Emerald Version"), "second");
	CHECK(!strcmp(g[2].title, "Freezai's Mountain & More") && !strcmp(g[2].platform, "Nintendo Game Boy"), "entities");
	free(h);

	h = slurp("tools/fixtures/lbox/images-2243.html");
	n = lbox_parse_images(h, im, 40, NULL);
	printf("lbox: %d cover pictures for Pokemon Emerald\n", n);
	CHECK(n >= 3, "some cover pictures");
	CHECK(n > 0 && !strcmp(im[0].kind, "Box - Front") && !strcmp(im[0].region, "North America"),
	      "a North American box front first");
	CHECK(n > 0 && !strncmp(im[0].url, "https://images.launchbox-app.com/", 33), "its picture's address");
	CHECK(n > 0 && im[0].thumb[0], "and a thumbnail");
	for (i = 0; i < n; i++) {
		CHECK(strcmp(im[i].kind, "Box - Back") && strncmp(im[i].kind, "Screenshot - Gameplay", 21) &&
		      strncmp(im[i].kind, "Clear Logo", 10), "no backs, gameplay or logos");
		if (i < 6) printf("  %s (%s)\n", im[i].kind, im[i].region);
	}
	n = lbox_parse_images(h, im, 40, "Germany");
	CHECK(n > 0 && !strcmp(im[0].region, "Germany"), "a preferred region first");
	CHECK(lbox_platform("gba") && !strcmp(lbox_platform("gba"), "Nintendo Game Boy Advance"), "gba's platform");
	free(h);
	printf(fails ? "lbox: %d FAILED\n" : "lbox: all passed\n", fails);
	return fails != 0;
}
