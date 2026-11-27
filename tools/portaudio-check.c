/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ports' sound, played through the libraries ports really use on the Brick,
 * with ChaOS's ALSA helper preloaded as run.sh preloads it.
 *
 * What broke: the helper (tools/alsafix) passed ALSA's calls on with
 * RTLD_NEXT, which can't see an ALSA that SDL or OpenAL Soft opened for
 * itself; that ALSA's own calls still came to the helper and failed, and no
 * such port had any sound in 2.8.0-test3 (Gone Fishing, Celeste Classic,
 * DomeRomantik). Its own check had only stood in for an ALSA a port links,
 * so it passed. This runs the Brick's own builds (arm64, under qemu-user):
 * its libasound and ALSA configuration (tools/fixtures/brick-alsa), and the
 * OpenAL Soft and SDL that ChaOS ships for ports (dist/brick/portlibs.tar.xz),
 * each playing half a second to ALSA's null device through plug, so ALSA's
 * plugins call the wrapped functions themselves, as dmix does on the Brick.
 *
 *   portaudio-check alsa|openal|sdl|default
 *
 * No headers for either library: they're opened by name, as ports' own
 * runtimes open them, and their few constants are written out here. Exit 0
 * when the sound played. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void nap(int ms)
{
	struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
	nanosleep(&t, NULL);
}

static void *sym(void *h, const char *n)
{
	void *f = h ? dlsym(h, n) : NULL;
	if (!f) fprintf(stderr, "portaudio-check: no %s\n", n);
	return f;
}

/* OpenAL Soft, as LÖVE and Gone Fishing use it */
static int openal(void)
{
	void *h = dlopen("libopenal.so.1", RTLD_NOW | RTLD_LOCAL);
	void *(*open_dev)(const char *);
	void *(*mk_ctx)(void *, const int *);
	char (*current)(void *);
	void (*get_ci)(void *, int, int, int *);
	void (*gen_buf)(int, unsigned *), (*gen_src)(int, unsigned *);
	void (*buf_data)(unsigned, int, const void *, int, int);
	void (*src_i)(unsigned, int, int), (*play)(unsigned);
	void (*get_si)(unsigned, int, int *);
	static int16_t pcm[44100];
	unsigned buf, src;
	int connected = 0, offset = 0, state = 0, i;
	void *dev, *ctx;

	if (!h) { fprintf(stderr, "portaudio-check: %s\n", dlerror()); return 1; }
	open_dev = (void *(*)(const char *))sym(h, "alcOpenDevice");
	mk_ctx = (void *(*)(void *, const int *))sym(h, "alcCreateContext");
	current = (char (*)(void *))sym(h, "alcMakeContextCurrent");
	get_ci = (void (*)(void *, int, int, int *))sym(h, "alcGetIntegerv");
	gen_buf = (void (*)(int, unsigned *))sym(h, "alGenBuffers");
	gen_src = (void (*)(int, unsigned *))sym(h, "alGenSources");
	buf_data = (void (*)(unsigned, int, const void *, int, int))sym(h, "alBufferData");
	src_i = (void (*)(unsigned, int, int))sym(h, "alSourcei");
	play = (void (*)(unsigned))sym(h, "alSourcePlay");
	get_si = (void (*)(unsigned, int, int *))sym(h, "alGetSourcei");
	if (!open_dev || !mk_ctx || !current || !get_ci || !gen_buf || !gen_src || !buf_data ||
	    !src_i || !play || !get_si)
		return 1;
	if (!(dev = open_dev(NULL))) { fprintf(stderr, "portaudio-check: OpenAL opened no device\n"); return 1; }
	if (!(ctx = mk_ctx(dev, NULL)) || !current(ctx)) {
		fprintf(stderr, "portaudio-check: OpenAL made no context\n");
		return 1;
	}
	for (i = 0; i < 44100; i++) pcm[i] = (int16_t)((i % 100 < 50) ? 3000 : -3000);
	gen_buf(1, &buf);
	buf_data(buf, 0x1101 /* AL_FORMAT_MONO16 */, pcm, (int)sizeof pcm, 44100);
	gen_src(1, &src);
	src_i(src, 0x1009 /* AL_BUFFER */, (int)buf);
	play(src);
	nap(500);
	get_ci(dev, 0x313 /* ALC_CONNECTED */, 1, &connected);
	get_si(src, 0x1025 /* AL_SAMPLE_OFFSET */, &offset);
	get_si(src, 0x1010 /* AL_SOURCE_STATE */, &state);
	/* the null device takes all it's given at once, so the second of sound
	 * is played out (AL_STOPPED) or under way */
	if (!connected || (offset <= 0 && state != 0x1014 /* AL_STOPPED */)) {
		fprintf(stderr, "portaudio-check: OpenAL played nothing (connected %d, state 0x%x, %d samples)\n",
		        connected, state, offset);
		return 1;
	}
	printf("ok: OpenAL Soft plays\n");
	return 0;
}

/* SDL's own audio, as SDL_mixer, Celeste Classic and most ports use it */
typedef struct {
	int freq;
	uint16_t format;
	uint8_t channels, silence;
	uint16_t samples, padding;
	uint32_t size;
	void (*callback)(void *, uint8_t *, int);
	void *userdata;
} sdl_spec;

static volatile int asked;

static void fill(void *u, uint8_t *s, int n)
{
	(void)u;
	memset(s, 0, (size_t)n);
	asked += n;
}

static int sdl(void)
{
	void *h = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
	int (*init)(uint32_t);
	uint32_t (*open_dev)(const char *, int, const sdl_spec *, sdl_spec *, int);
	void (*pause)(uint32_t, int);
	const char *(*err)(void);
	const char *(*driver)(void);
	sdl_spec want = { 44100, 0x8010 /* AUDIO_S16LSB */, 2, 0, 1024, 0, 0, fill, NULL }, have;
	uint32_t id;
	int first, bytes, status;
	int (*get_status)(uint32_t);

	if (!h) { fprintf(stderr, "portaudio-check: %s\n", dlerror()); return 1; }
	init = (int (*)(uint32_t))sym(h, "SDL_Init");
	open_dev = (uint32_t (*)(const char *, int, const sdl_spec *, sdl_spec *, int))sym(h, "SDL_OpenAudioDevice");
	pause = (void (*)(uint32_t, int))sym(h, "SDL_PauseAudioDevice");
	err = (const char *(*)(void))sym(h, "SDL_GetError");
	driver = (const char *(*)(void))sym(h, "SDL_GetCurrentAudioDriver");
	get_status = (int (*)(uint32_t))sym(h, "SDL_GetAudioDeviceStatus");
	if (!init || !open_dev || !pause || !err || !driver || !get_status) return 1;
	if (init(0x10 /* SDL_INIT_AUDIO */) != 0) {
		fprintf(stderr, "portaudio-check: SDL's audio didn't start: %s\n", err());
		return 1;
	}
	if (!driver() || strcmp(driver(), "alsa") != 0) {
		fprintf(stderr, "portaudio-check: SDL chose %s, not ALSA\n", driver() ? driver() : "nothing");
		return 1;
	}
	if (!(id = open_dev(NULL, 0, &want, &have, 0))) {
		fprintf(stderr, "portaudio-check: SDL opened no device: %s\n", err());
		return 1;
	}
	pause(id, 0);
	/* still asking for sound in the second quarter second */
	nap(250);
	first = asked;
	nap(250);
	/* a stream that failed is given up (SDL_AUDIO_STOPPED), though SDL goes
	 * on asking the port for sound so it doesn't hang */
	status = get_status(id);
	pause(id, 1);
	bytes = asked;
	if (status != 1 /* SDL_AUDIO_PLAYING */ || first <= 0 || bytes <= first) {
		fprintf(stderr, "portaudio-check: SDL's sound stopped (status %d; %d bytes, then %d)\n",
		        status, first, bytes - first);
		return 1;
	}
	printf("ok: SDL plays through ALSA\n");
	return 0;
}

/* ALSA itself, as a port that links it does (Doukutsu-rs's cpal): it is
 * among the program's own libraries, where RTLD_NEXT finds it */
static int alsa(void)
{
	void *h = dlopen("libasound.so.2", RTLD_NOW | RTLD_GLOBAL);
	int (*open_pcm)(void **, const char *, int, int);
	int (*set)(void *, int, int, unsigned, unsigned, int, unsigned);
	long (*writei)(void *, const void *, unsigned long);
	static int16_t pcm[2 * 441];
	void *p;
	long r = 0;
	int i, written = 0;

	if (!h) { fprintf(stderr, "portaudio-check: %s\n", dlerror()); return 1; }
	open_pcm = (int (*)(void **, const char *, int, int))sym(h, "snd_pcm_open");
	set = (int (*)(void *, int, int, unsigned, unsigned, int, unsigned))sym(h, "snd_pcm_set_params");
	writei = (long (*)(void *, const void *, unsigned long))sym(h, "snd_pcm_writei");
	if (!open_pcm || !set || !writei) return 1;
	if (open_pcm(&p, "default", 0, 0) < 0 ||
	    set(p, 2 /* S16_LE */, 3 /* RW_INTERLEAVED */, 2, 44100, 1, 100000) < 0) {
		fprintf(stderr, "portaudio-check: ALSA opened no device\n");
		return 1;
	}
	for (i = 0; i < 100 && (r = writei(p, pcm, 441)) > 0; i++) written += (int)r;
	if (r < 0) {
		fprintf(stderr, "portaudio-check: ALSA stopped after %d frames (%ld)\n", written, r);
		return 1;
	}
	printf("ok: ALSA plays, linked as a port links it\n");
	return 0;
}

/* What a port's default device plays to, as ALSA reads its configuration:
 * printed, for tools/portaudio-check.sh to hold against run.sh's log */
static int config(void)
{
	void *h = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
	int (*update)(void);
	int (*search)(void *, const char *, void **);
	int (*get_str)(void *, const char **);
	void **top;
	void *n;
	const char *v = NULL;

	if (!h) { fprintf(stderr, "portaudio-check: %s\n", dlerror()); return 1; }
	update = (int (*)(void))sym(h, "snd_config_update");
	search = (int (*)(void *, const char *, void **))sym(h, "snd_config_search");
	get_str = (int (*)(void *, const char **))sym(h, "snd_config_get_string");
	top = (void **)sym(h, "snd_config");
	if (!update || !search || !get_str || !top) return 1;
	if (update() < 0 || !*top) { fprintf(stderr, "portaudio-check: no configuration\n"); return 1; }
	if (search(*top, "pcm.default.slave.pcm", &n) < 0 || get_str(n, &v) < 0 || !v) {
		fprintf(stderr, "portaudio-check: the default device has no slave named\n");
		return 1;
	}
	printf("%s\n", v);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "default")) return config();
	if (argc == 2 && !strcmp(argv[1], "alsa")) return alsa();
	if (argc == 2 && !strcmp(argv[1], "openal")) return openal();
	if (argc == 2 && !strcmp(argv[1], "sdl")) return sdl();
	fprintf(stderr, "usage: portaudio-check alsa|openal|sdl|default\n");
	return 2;
}
