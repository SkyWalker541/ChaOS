/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Button Mapping. See remap.h. */
#include "remap.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *const BTN_ID[RM_BUTTONS] = {
	"a", "b", "x", "y", "l1", "r1", "l2", "r2", "select", "start"
};
static const char *const BTN_LABEL[RM_BUTTONS] = {
	"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "Select", "Start"
};

const char *rm_button_id(int i)    { return i >= 0 && i < RM_BUTTONS ? BTN_ID[i] : ""; }
const char *rm_button_label(int i) { return i >= 0 && i < RM_BUTTONS ? BTN_LABEL[i] : ""; }

/* Each console's pad, as its core names the retropad's buttons (their input
 * descriptors), in the order a player reads them */
typedef struct { const char *target, *label; } pad_btn;
typedef struct { const char *tags; pad_btn btn[9]; } pad;

static const pad PADS[] = {
	/* FCEUmm, mGBA */
	{ "NES GB GBC", { { "a", "A" }, { "b", "B" }, { "select", "Select" },
	                  { "start", "Start" } } },
	/* mGBA's own turbo L and R are on the retropad's L2 and R2 */
	{ "GBA", { { "a", "A" }, { "b", "B" }, { "l1", "L" }, { "r1", "R" },
	           { "select", "Select" }, { "start", "Start" },
	           { "l2", "Turbo L" }, { "r2", "Turbo R" } } },
	/* Snes9x 2010 */
	{ "SFC", { { "a", "A" }, { "b", "B" }, { "x", "X" }, { "y", "Y" },
	           { "l1", "L" }, { "r1", "R" }, { "select", "Select" },
	           { "start", "Start" } } },
	/* Genesis Plus GX: its six-button pad on the retropad (libretro.c's
	 * descriptors: Y is A, B is B, A is C, L is X, X is Y, R is Z) */
	{ "MD", { { "y", "A" }, { "b", "B" }, { "a", "C" }, { "l1", "X" },
	          { "x", "Y" }, { "r1", "Z" }, { "select", "Mode" },
	          { "start", "Start" } } },
	/* Genesis Plus GX's Master System and Game Gear: Button 1 and 2 */
	{ "SMS", { { "b", "1" }, { "a", "2" }, { "start", "Pause" } } },
	{ "GG", { { "b", "1" }, { "a", "2" }, { "start", "Start" } } },
	/* Beetle PCE Fast */
	{ "PCE", { { "a", "I" }, { "b", "II" }, { "select", "Select" },
	           { "start", "Run" } } },
	/* Beetle NeoPop */
	{ "NGP NGPC", { { "a", "A" }, { "b", "B" }, { "start", "Option" } } },
};

static const pad *pad_of(const char *tag)
{
	size_t i;
	char want[16];

	snprintf(want, sizeof want, " %s ", tag ? tag : "");
	for (i = 0; i < sizeof PADS / sizeof PADS[0]; i++) {
		char tags[64];
		snprintf(tags, sizeof tags, " %s ", PADS[i].tags);
		if (strstr(tags, want)) return &PADS[i];
	}
	return &PADS[2];       /* the SNES pad: every face button and shoulder */
}

int rm_choices(const char *tag, rm_choice *out)
{
	const pad *p = pad_of(tag);
	int n = 0, i;

	for (i = 0; i < 9 && p->btn[i].target; i++)
		out[n++] = (rm_choice){ p->btn[i].target, p->btn[i].label, false };
	for (i = 0; i < 9 && p->btn[i].target; i++)
		if (strcmp(p->btn[i].target, "select") && strcmp(p->btn[i].target, "start") &&
		    strncmp(p->btn[i].label, "Turbo", 5))
			out[n++] = (rm_choice){ p->btn[i].target, p->btn[i].label, true };
	out[n++] = (rm_choice){ "none", "None", false };
	return n;
}

static int choice_of(const rm_choice *c, int n, const char *target, bool turbo)
{
	int i;
	for (i = 0; i < n; i++)
		if (!strcmp(c[i].target, target) && c[i].turbo == turbo) return i;
	return n - 1;          /* None */
}

void rm_from_spec(const char *tag, const char *spec, rm_map *m)
{
	rm_choice c[RM_CHOICES];
	int n = rm_choices(tag, c), i;
	char buf[512], *save = NULL, *pair;

	/* Each button as itself first, which is what an empty map means */
	for (i = 0; i < RM_BUTTONS; i++) m->choice[i] = choice_of(c, n, BTN_ID[i], false);
	if (!spec || !*spec || !strcmp(spec, "identity")) return;
	snprintf(buf, sizeof buf, "%s", spec);
	for (pair = strtok_r(buf, ",", &save); pair; pair = strtok_r(NULL, ",", &save)) {
		char *colon = strchr(pair, ':'), *tilde;
		if (!colon) continue;
		*colon = '\0';
		tilde = strchr(colon + 1, '~');
		if (tilde) *tilde = '\0';
		for (i = 0; i < RM_BUTTONS; i++)
			if (!strcmp(pair, BTN_ID[i]))
				m->choice[i] = choice_of(c, n, colon + 1, tilde != NULL);
	}
}

bool rm_to_spec(const char *tag, const rm_map *m, char *out, int outn)
{
	rm_choice c[RM_CHOICES];
	int n = rm_choices(tag, c), i, at = 0;

	out[0] = '\0';
	for (i = 0; i < RM_BUTTONS; i++) {
		int k = m->choice[i] >= 0 && m->choice[i] < n ? m->choice[i] : n - 1;
		int w;

		if (c[k].turbo)
			w = snprintf(out + at, outn - at, "%s%s:%s~%d", at ? "," : "",
			             BTN_ID[i], c[k].target, RM_TURBO);
		else
			w = snprintf(out + at, outn - at, "%s%s:%s", at ? "," : "",
			             BTN_ID[i], c[k].target);
		if (w < 0 || w >= outn - at) return false;
		at += w;
	}
	return true;
}

const char *rm_choice_label(const char *tag, int choice, char *buf, int n)
{
	rm_choice c[RM_CHOICES];
	int k = rm_choices(tag, c);

	if (choice < 0 || choice >= k) choice = k - 1;
	if (c[choice].turbo) snprintf(buf, n, "Turbo %s", c[choice].label);
	else snprintf(buf, n, "%s", c[choice].label);
	return buf;
}
