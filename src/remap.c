/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Button Mapping. See remap.h. */
#include "remap.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

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
typedef struct { const char *tags; pad_btn btn[RM_BUTTONS]; } pad;

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
	/* ChaOS: PCSX-ReARMed, the PlayStation pad (its input descriptors: B is
	 * Cross, A Circle, Y Square, X Triangle) */
	{ "PS", { { "b", "Cross" }, { "a", "Circle" }, { "y", "Square" }, { "x", "Triangle" },
	          { "l1", "L1" }, { "r1", "R1" }, { "l2", "L2" }, { "r2", "R2" },
	          { "select", "Select" }, { "start", "Start" } } },
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

	for (i = 0; i < RM_BUTTONS && p->btn[i].target; i++)
		out[n++] = (rm_choice){ p->btn[i].target, p->btn[i].label, false };
	for (i = 0; i < RM_BUTTONS && p->btn[i].target; i++)
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

	bool timed = false;

	/* Each button as itself first, which is what an empty map means */
	for (i = 0; i < RM_BUTTONS; i++) m->choice[i] = choice_of(c, n, BTN_ID[i], false);
	m->period = RM_TURBO;
	if (!spec || !*spec || !strcmp(spec, "identity")) return;
	snprintf(buf, sizeof buf, "%s", spec);
	for (pair = strtok_r(buf, ",", &save); pair; pair = strtok_r(NULL, ",", &save)) {
		char *colon = strchr(pair, ':'), *tilde;
		if (!colon) continue;
		*colon = '\0';
		tilde = strchr(colon + 1, '~');
		if (tilde) {
			char *end;
			long v = strtol(tilde + 1, &end, 10);

			*tilde = '\0';
			/* The map's speed is its first turbo button's: ChaOS writes one
			 * for all of them. Anything Diatom wouldn't take reads as Normal. */
			if (!timed && end != tilde + 1 && !*end && v >= 1 && v <= RM_PERIOD_MAX) {
				m->period = (int)v;
				timed = true;
			}
		}
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
			             BTN_ID[i], c[k].target,
			             m->period >= 1 && m->period <= RM_PERIOD_MAX ? m->period : RM_TURBO);
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

/* Half a press, in frames: at 60 a second, 6, 10, 15 and 30 presses a
 * second - a turbo pad's slow and fast settings either side of the shipped
 * speed, and the fastest a game reading every frame can see */
static const struct { int period; const char *name; int per_s; } SPEED[RM_SPEEDS] = {
	{ 5, "Slow", 6 }, { RM_TURBO, "Normal", 10 }, { 2, "Fast", 15 }, { 1, "Fastest", 30 }
};

bool rm_has_turbo(const char *tag, const rm_map *m)
{
	rm_choice c[RM_CHOICES];
	int n = rm_choices(tag, c), i;

	for (i = 0; i < RM_BUTTONS; i++)
		if (m->choice[i] >= 0 && m->choice[i] < n && c[m->choice[i]].turbo) return true;
	return false;
}

const char *rm_speed_label(int period, char *buf, int n)
{
	int i;

	for (i = 0; i < RM_SPEEDS; i++)
		if (SPEED[i].period == period) {
			snprintf(buf, n, "%s, %d/s", SPEED[i].name, SPEED[i].per_s);
			return buf;
		}
	snprintf(buf, n, "Custom");
	return buf;
}

int rm_speed_step(int period, int d)
{
	int i;

	for (i = 0; i < RM_SPEEDS; i++)
		if (SPEED[i].period == period)
			return SPEED[(i + (d < 0 ? -1 : 1) + RM_SPEEDS) % RM_SPEEDS].period;
	/* custom: faster is a shorter period, the presets run slowest first */
	if (d > 0) {
		for (i = 0; i < RM_SPEEDS; i++)
			if (SPEED[i].period < period) return SPEED[i].period;
		return SPEED[0].period;
	}
	for (i = RM_SPEEDS - 1; i >= 0; i--)
		if (SPEED[i].period > period) return SPEED[i].period;
	return SPEED[RM_SPEEDS - 1].period;
}

int rm_rows(bool scope, int *ids, int max)
{
	int n = 0, i;

#define ADD(x) do { if (n < max) ids[n] = (x); n++; } while (0)
	if (scope) { ADD(RM_ROW_SCOPE); ADD(RM_ROW_NONE); }
	for (i = 0; i < RM_BUTTONS; i++) ADD(i);
	ADD(RM_ROW_NONE);
	ADD(RM_ROW_SPEED);
	ADD(RM_ROW_NONE);
	ADD(RM_ROW_RESET);
#undef ADD
	return n < max ? n : max;
}
