/* ChaOS: PICO-8's files and carts (src/pico8.h), on files made here */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/library.h"
#include "../src/pico8.h"

static int fails;
static void ck(int c, const char *what) { if (!c) { printf("  FAIL %s\n", what); fails++; } }

/* An ELF header: class, byte order, machine */
static void elf(const char *path, int cls, int msb, unsigned m)
{
	unsigned char h[64] = { 0x7f, 'E', 'L', 'F' };
	FILE *f = fopen(path, "wb");

	h[4] = (unsigned char)cls; h[5] = msb ? 2 : 1;
	if (msb) { h[18] = (unsigned char)(m >> 8); h[19] = (unsigned char)m; }
	else     { h[18] = (unsigned char)m; h[19] = (unsigned char)(m >> 8); }
	fwrite(h, 1, sizeof h, f);
	fclose(f);
}

static void put(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");
	fputs(s, f);
	fclose(f);
}

int main(void)
{
	char base[] = "/tmp/pico8-check-XXXXXX", d[600], p[800], zip[800];
	game_list l;
	int i;

	if (!mkdtemp(base)) return 1;
	snprintf(d, sizeof d, "%s/PICO-8", base);
	mkdir(d, 0755);

	ck(p8_check(d) == P8_NONE, "an empty folder has nothing");
	snprintf(p, sizeof p, "%s/pico8.dat", d); put(p, "data");
	ck(p8_check(d) == P8_NO_BIN, "the data alone wants the program");
	snprintf(p, sizeof p, "%s/pico8_64", d);
	elf(p, 2, 0, 62);
	ck(p8_check(d) == P8_WRONG_CPU, "an x86-64 pico8_64 is the wrong one");
	ck(p8_elf_arm(p) == -1, "x86-64 isn't ARM");
	elf(p, 1, 0, 40);
	ck(p8_elf_arm(p) == 32, "32-bit ARM is told apart");
	ck(p8_check(d) == P8_WRONG_CPU, "a 32-bit pico8 under pico8_64's name is the wrong one");
	elf(p, 2, 0, 183);
	ck(p8_elf_arm(p) == 64, "64-bit ARM is 64");
	ck(p8_check(d) == P8_READY, "the program and its data are ready");
	snprintf(p, sizeof p, "%s/pico8.dat", d); unlink(p);
	ck(p8_check(d) == P8_NO_DAT, "the program alone wants its data");
	for (i = P8_READY; i <= P8_WRONG_CPU; i++) ck(p8_status_say((p8_status)i)[0], "every status has words");

	/* the download's zip, found among others, the newest */
	snprintf(p, sizeof p, "%s/pico-8_0.2.5g_raspi.zip", d); put(p, "z");
	snprintf(p, sizeof p, "%s/pico-8_0.2.6b_raspi.zip", d); put(p, "z");
	snprintf(p, sizeof p, "%s/pico-8_0.2.6b_windows.zip", d); put(p, "z");
	snprintf(p, sizeof p, "%s/something_raspi.zip", d); put(p, "z");
	ck(p8_find_zip(d, zip, sizeof zip) && strstr(zip, "0.2.6b_raspi"), "the newest Raspberry Pi zip");

	/* what an unzip leaves: pico-8/ with the files in it, taken out */
	{
		char st[700], in[760];

		snprintf(p, sizeof p, "%s/pico8_64", d); unlink(p);
		snprintf(st, sizeof st, "%s/.unpack", d);
		mkdir(st, 0755);
		snprintf(in, sizeof in, "%s/pico-8", st);
		mkdir(in, 0755);
		snprintf(p, sizeof p, "%s/pico8_64", in); elf(p, 2, 0, 183);
		ck(!p8_take_from(st, d), "half a download isn't taken");
		snprintf(p, sizeof p, "%s/pico8.dat", in); put(p, "data");
		snprintf(p, sizeof p, "%s/pico8", in); elf(p, 1, 0, 40);
		ck(p8_take_from(st, d), "a whole download is taken");
		ck(p8_check(d) == P8_READY, "and is ready after");
	}

	/* carts */
	ck(p8_is_cart("celeste.p8.png") && p8_is_cart("Jelpi.P8") && !p8_is_cart("notes.png") &&
	   !p8_is_cart("p8.txt"), "carts are .p8 and .p8.png");
	{
		char roms[700];

		snprintf(roms, sizeof roms, "%s/Roms/PICO-8", base);
		snprintf(p, sizeof p, "%s/Roms", base); mkdir(p, 0755);
		mkdir(roms, 0755);
		snprintf(p, sizeof p, "%s/celeste.p8.png", roms); put(p, "c");
		snprintf(p, sizeof p, "%s/jelpi.p8", roms); put(p, "c");
		snprintf(p, sizeof p, "%s/label.png", roms); put(p, "c");
		memset(&l, 0, sizeof l);
		snprintf(p, sizeof p, "%s/Roms", base);
		lib_scan(p, "PICO-8", "p8,png", &l);
		ck(l.count == 2, "two carts on the shelf, not the plain png");
		for (i = 0; i < l.count; i++) {
			char m[1200];
			snprintf(m, sizeof m, "%s is named %s", l.items[i].file, l.items[i].name);
			ck(!strcmp(l.items[i].name, "celeste") || !strcmp(l.items[i].name, "jelpi"), m);
		}
		lib_free(&l);
	}

	snprintf(p, sizeof p, "rm -rf '%s'", base);
	if (system(p)) {}
	if (fails) { printf("pico8: %d failures\n", fails); return 1; }
	printf("ok: PICO-8's files, its download and its carts\n");
	return 0;
}
