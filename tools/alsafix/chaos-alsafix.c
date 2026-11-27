/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: a port's ALSA stream brought back after the power menu froze it.
 *
 * The power menu stops the port (SIGSTOP) while it asks, and its sound
 * stream runs dry meanwhile. A port that plays through cpal (Doukutsu-rs, a
 * Rust audio library) then found snd_pcm_poll_descriptors failing with EPIPE
 * - an underrun cpal doesn't look for there - and played nothing again for
 * the rest of the game (Aquilus, 2.7.9; again in 2.8.0-test2). Here, only
 * when such a call fails, the stream is dropped and prepared and the call
 * made again; and the frames free, through ALSA's rate converter (44.1 kHz
 * ports on the 48 kHz codec), which read as more than memory holds after
 * that, are never said to be more than the buffer - cpal asked for a buffer
 * that size and stopped ("capacity overflow").
 *
 * Preloaded for every port (res/ports/run.sh). It links no ALSA of its own:
 * every call goes to the ALSA the port already has, found at run time, so a
 * port that carries its own libasound is served by that one. A port whose
 * SDL opens ALSA itself (dlopen) isn't reached at all, and needs nothing.
 * In ordinary play each wrapped call costs one comparison. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <poll.h>
#include <stdio.h>
#include <sys/types.h>

typedef struct _snd_pcm snd_pcm_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;

static int said;

static void *next(const char *name) { return dlsym(RTLD_NEXT, name); }

/* The stream started over, with the port's own ALSA */
static int reset(snd_pcm_t *pcm, const char *call, int r)
{
	int (*drop)(snd_pcm_t *) = next("snd_pcm_drop");
	int (*prepare)(snd_pcm_t *) = next("snd_pcm_prepare");
	int p = -1;

	if (drop) drop(pcm);
	if (prepare) p = prepare(pcm);
	if (said++ < 20)
		fprintf(stderr, "chaos-alsafix: %s gave %d; the stream started over (%d)\n", call, r, p);
	return p;
}

int snd_pcm_poll_descriptors(snd_pcm_t *pcm, struct pollfd *pfds, unsigned int space)
{
	static int (*real)(snd_pcm_t *, struct pollfd *, unsigned int);
	int r;

	if (!real && !(real = next("snd_pcm_poll_descriptors"))) return -1;
	r = real(pcm, pfds, space);
	if (r >= 0 || reset(pcm, "snd_pcm_poll_descriptors", r) < 0) return r;
	return real(pcm, pfds, space);
}

int snd_pcm_poll_descriptors_count(snd_pcm_t *pcm)
{
	static int (*real)(snd_pcm_t *);
	int r;

	if (!real && !(real = next("snd_pcm_poll_descriptors_count"))) return -1;
	r = real(pcm);
	if (r >= 0 || reset(pcm, "snd_pcm_poll_descriptors_count", r) < 0) return r;
	return real(pcm);
}

int snd_pcm_poll_descriptors_revents(snd_pcm_t *pcm, struct pollfd *pfds, unsigned int nfds,
                                     unsigned short *revents)
{
	static int (*real)(snd_pcm_t *, struct pollfd *, unsigned int, unsigned short *);
	int r;

	if (!real && !(real = next("snd_pcm_poll_descriptors_revents"))) return -1;
	r = real(pcm, pfds, nfds, revents);
	if (r >= 0 || reset(pcm, "snd_pcm_poll_descriptors_revents", r) < 0) return r;
	return real(pcm, pfds, nfds, revents);
}

/* Never more free than the whole buffer. Only a figure past any real one
 * (16M frames, six minutes at 48 kHz) is looked at further. */
static snd_pcm_sframes_t cap(snd_pcm_t *pcm, snd_pcm_sframes_t v, const char *call)
{
	int (*params)(snd_pcm_t *, snd_pcm_uframes_t *, snd_pcm_uframes_t *);
	snd_pcm_uframes_t buf = 0, per = 0;

	if (v <= (1L << 24) || !(params = next("snd_pcm_get_params")) || params(pcm, &buf, &per) < 0 ||
	    !buf || (snd_pcm_uframes_t)v <= buf)
		return v;
	if (said++ < 20)
		fprintf(stderr, "chaos-alsafix: %s gave %ld, more than the buffer's %lu; said %lu\n", call,
		        v, buf, buf);
	return (snd_pcm_sframes_t)buf;
}

snd_pcm_sframes_t snd_pcm_avail(snd_pcm_t *pcm)
{
	static snd_pcm_sframes_t (*real)(snd_pcm_t *);

	if (!real && !(real = next("snd_pcm_avail"))) return -1;
	return cap(pcm, real(pcm), "snd_pcm_avail");
}

snd_pcm_sframes_t snd_pcm_avail_update(snd_pcm_t *pcm)
{
	static snd_pcm_sframes_t (*real)(snd_pcm_t *);

	if (!real && !(real = next("snd_pcm_avail_update"))) return -1;
	return cap(pcm, real(pcm), "snd_pcm_avail_update");
}
