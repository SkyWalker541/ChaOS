/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: src/libpack.c - what a port's programs need, against a device.
 *
 *   make check-libpack [LIBPACK_PORT=dir LIBPACK_HAVE=dir]
 *
 * Builds a small port of its own (a program, a library it carries and one
 * it carries but never loads) with the host compiler, and checks that only
 * what the program reaches and nothing has is missing. Given a real port's
 * folder and a folder named like the Brick's libraries, lists what it lacks. */
#include "libpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	char dir[] = "/tmp/libpack-check-XXXXXX", cmd[2048], miss[16][LIBPACK_NAME], need[16][LIBPACK_NAME];
	char so[LIBPACK_NAME], have[600], prog[600];
	const char *dirs[2];
	const char *rp = getenv("LIBPACK_PORT"), *rh = getenv("LIBPACK_HAVE");
	int n, k;

	if (!mkdtemp(dir)) return 1;
	snprintf(cmd, sizeof cmd,
	         "cd %s && mkdir -p port/libs have && "
	         "printf 'int z(void); int a(void){return z();}' > a.c && printf 'int z(void){return 2;}' > z.c && "
	         "printf 'int b(void){return 3;}' > b.c && "
	         "touch have/libc.so.6 have/ld-linux-x86-64.so.2 have/ld-linux-aarch64.so.1 && cc -shared -fPIC -o have/libhave.so.1 -Wl,-soname,libhave.so.1 b.c && "
	         "cc -shared -fPIC -o libgone.so.2 -Wl,-soname,libgone.so.2 z.c && "
	         "cc -shared -fPIC -o port/libs/libcarried.so.1 -Wl,-soname,libcarried.so.1 a.c "
	         "  -L. -l:libgone.so.2 && "
	         "cc -shared -fPIC -o port/libs/libunused.so -Wl,-soname,libunused.so a.c -L. -l:libgone.so.2 && "
	         "printf 'int a(void); int b(void); int main(void){return a()+b();}' > m.c && "
	         "cc -o port/game m.c -Lport/libs -Lhave -Wl,-rpath-link,. -l:libcarried.so.1 -l:libhave.so.1 && "
	         "echo data > port/level.dat", dir);
	if (system(cmd) != 0) { printf("FAIL: couldn't build the test port\n"); return 1; }
	snprintf(prog, sizeof prog, "%s/port/game", dir);
	CHECK(libpack_elf(prog, need, 16, &n, so, sizeof so), "the program read");
	for (k = 0; k < n && strcmp(need[k], "libcarried.so.1"); k++) ;
	CHECK(k < n, "it needs libcarried.so.1");
	snprintf(prog, sizeof prog, "%s/port/libs/libcarried.so.1", dir);
	CHECK(libpack_elf(prog, need, 16, &n, so, sizeof so) && !strcmp(so, "libcarried.so.1"),
	      "a library's soname: %s", so);
	snprintf(prog, sizeof prog, "%s/port/level.dat", dir);
	CHECK(!libpack_elf(prog, need, 16, &n, so, sizeof so), "not an ELF");

	snprintf(have, sizeof have, "%s/have", dir);
	dirs[0] = have;
	dirs[1] = NULL;
	snprintf(prog, sizeof prog, "%s/port", dir);
	n = libpack_missing(prog, dirs, miss, 16);
	CHECK(n == 1 && !strcmp(miss[0], "libgone.so.2"),
	      "missing: only what the carried library needs (%d%s%s)", n, n ? ", " : "", n ? miss[0] : "");
	{
		const char *list = "{\"version\":1,\"libs\":{\"libgone.so.2\":{\"file\":\"libgone.so.2\","
		                   "\"md5\":\"abc\",\"size\":7},\"bad\":{\"file\":\"../x\"}}}";
		char file[64], md5[40];
		long sz;

		CHECK(libpack_find(list, strlen(list), "libgone.so.2", file, sizeof file, md5, sizeof md5, &sz)
		      && !strcmp(file, "libgone.so.2") && !strcmp(md5, "abc") && sz == 7, "found in the list");
		CHECK(!libpack_find(list, strlen(list), "libnot.so", file, sizeof file, md5, sizeof md5, &sz),
		      "not in the list");
		CHECK(!libpack_find(list, strlen(list), "bad", file, sizeof file, md5, sizeof md5, &sz),
		      "a file with a path in it refused");
	}
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	if (system(cmd) != 0) fails++;

	if (rp && rh) {
		const char *d2[] = { rh, NULL };

		n = libpack_missing(rp, d2, miss, 16);
		printf("  %s lacks %d:", rp, n);
		for (k = 0; k < n; k++) printf(" %s", miss[k]);
		printf("\n");
	}
	if (fails) { printf("libpack: %d failed\n", fails); return 1; }
	printf("libpack: ok\n");
	return 0;
}
