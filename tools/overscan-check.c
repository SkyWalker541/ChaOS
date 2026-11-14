/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Crop Overscan's table (src/overscan.c) */
#include <stdio.h>
#include <string.h>

#include "overscan.h"

static int fails;
#define ck(c, ...) do { if (!(c)) { fails++; printf("  FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static const char *store[8][2];
static int nstore;
static const char *get(const char *key, void *ctx)
{
	int i;
	(void)ctx;
	for (i = 0; i < nstore; i++) if (!strcmp(store[i][0], key)) return store[i][1];
	return NULL;
}

int main(void)
{
	const os_console *c;
	int i, k;

	ck(os_for("NES") && os_for("PCE") && os_for("SMS"), "NES, TurboGrafx-16 and Master System crop");
	ck(!os_for("MD") && !os_for("SFC") && !os_for("GB") && !os_for("GG") && !os_for(NULL),
	   "and nothing else does");
	c = os_for("NES");
	ck(os_current(c, get, NULL) == 1 && !strcmp(c->c[c->def].label, "Top & Bottom"),
	   "nothing stored is the core's own default");
	for (i = 0; i < c->n; i++) {
		nstore = 0;
		for (k = 0; k < OS_KEYS && c->c[i].key[k]; k++) {
			store[nstore][0] = c->c[i].key[k];
			store[nstore++][1] = c->c[i].value[k];
		}
		ck(os_current(c, get, NULL) == i, "a stored %s reads back as itself", c->c[i].label);
	}
	/* Every choice names the same options, so going between them leaves
	 * nothing of the last behind in an emulator that stays loaded */
	for (c = os_for("NES"); c; c = c == os_for("NES") ? os_for("PCE") : c == os_for("PCE") ? os_for("SMS") : NULL)
		for (i = 1; i < c->n; i++)
			for (k = 0; k < OS_KEYS; k++)
				ck((!c->c[i].key[k] && !c->c[0].key[k]) ||
				   (c->c[i].key[k] && c->c[0].key[k] && !strcmp(c->c[i].key[k], c->c[0].key[k])),
				   "%s: every choice sets the same options", c->tag);
	if (fails) { printf("overscan: %d failed\n", fails); return 1; }
	printf("ok: Crop Overscan's choices are complete and read back\n");
	return 0;
}
