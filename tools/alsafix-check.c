/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The ports' ALSA helper (tools/alsafix/chaos-alsafix.c), against a stand-in
 * ALSA that fails the way the Brick's did after the power menu froze a port.
 *
 * What broke: Doukutsu-rs's audio library (cpal) met EPIPE from
 * snd_pcm_poll_descriptors after the freeze and played nothing again; then,
 * once the stream was restarted, a free-frames figure past any real one made
 * it ask for a buffer that size and stop (2.8.0-test2). And then, with the
 * helper in, no port whose SDL or OpenAL opens ALSA itself (dlopen) had any
 * sound: that ALSA's own calls came to the helper, which couldn't find it to
 * pass them on (2.8.0-test3). Built four ways by the Makefile: this program,
 * the stand-in (-DFAKE_ALSA) as a library it links and as a libasound.so.2 it
 * opens itself (-DDLOPEN_CHECK), and the helper, preloaded. */
#include <stdio.h>
#include <stdlib.h>

typedef struct _snd_pcm snd_pcm_t;
struct pollfd;

#ifdef FAKE_ALSA
int fake_fail = 1, fake_drops, fake_prepares;
long fake_avail = 1000;
int snd_pcm_poll_descriptors(snd_pcm_t *p, struct pollfd *f, unsigned int n)
{ (void)p; (void)f; (void)n; if (fake_fail) return -32; return 1; }
int snd_pcm_drop(snd_pcm_t *p) { (void)p; fake_drops++; return 0; }
int snd_pcm_prepare(snd_pcm_t *p) { (void)p; fake_prepares++; fake_fail = 0; return 0; }
long snd_pcm_avail(snd_pcm_t *p) { (void)p; return fake_avail; }
int snd_pcm_get_params(snd_pcm_t *p, unsigned long *buf, unsigned long *per)
{ (void)p; *buf = 3763; *per = 940; return 0; }
int snd_pcm_poll_descriptors_count(snd_pcm_t *p) { (void)p; return 1; }
/* as dmix asks its hardware: ALSA calling its own exported function, which
 * the preloaded helper takes */
int fake_inner_count(snd_pcm_t *p) { return snd_pcm_poll_descriptors_count(p); }
#elif defined DLOPEN_CHECK
#include <dlfcn.h>
int main(void)
{
	/* as SDL and OpenAL Soft open it: by name, kept to themselves */
	void *h = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
	int (*inner)(snd_pcm_t *) = h ? (int (*)(snd_pcm_t *))dlsym(h, "fake_inner_count") : NULL;
	int r;

	if (!inner) { fprintf(stderr, "alsafix-check: the stand-in ALSA didn't open\n"); return 1; }
	r = inner((snd_pcm_t *)&h);
	if (r != 1) {
		fprintf(stderr, "alsafix-check: an ALSA the port opened itself got %d, not its own answer\n", r);
		return 1;
	}
	printf("ok: a port that opens ALSA itself still has sound\n");
	return 0;
}
#else
extern int fake_fail, fake_drops, fake_prepares;
extern long fake_avail;
int snd_pcm_poll_descriptors(snd_pcm_t *, struct pollfd *, unsigned int);
long snd_pcm_avail(snd_pcm_t *);

static int fails;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "alsafix-check: %s\n", m); fails++; } } while (0)

int main(void)
{
	snd_pcm_t *pcm = (snd_pcm_t *)&fails;

	CHECK(snd_pcm_poll_descriptors(pcm, NULL, 1) == 1, "the call wasn't made again after the stream started over");
	CHECK(fake_drops == 1 && fake_prepares == 1, "the stream wasn't dropped and prepared once");
	CHECK(snd_pcm_poll_descriptors(pcm, NULL, 1) == 1 && fake_prepares == 1, "a call that works was touched");
	CHECK(snd_pcm_avail(pcm) == 1000, "an ordinary free-frames figure was changed");
	fake_avail = 4233383649727719013L;
	CHECK(snd_pcm_avail(pcm) == 3763, "a figure past any real one wasn't held to the buffer");
	if (fails) return 1;
	printf("ok: a port's sound comes back after the power menu\n");
	return 0;
}
#endif
