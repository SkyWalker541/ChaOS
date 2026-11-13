/* SPDX-License-Identifier: MIT */
/* ChaOS: libpulse-simple.so.0 for ports, without PulseAudio.
 *
 * A port's own copy of a library - a MikMod, a FluidSynth - was often
 * built with PulseAudio output among its others, so it can't load at all on
 * a device with no libpulse-simple, even though it would never use it: the
 * Brick plays through ALSA (and SDL), and has no PulseAudio server for it
 * to reach anyway. This is the library's interface with nothing behind it:
 * every connection is refused, as it would be with no server, and the
 * caller goes on to its next way of playing sound.
 *
 * Built by tools/build-libpack.sh with -soname libpulse-simple.so.0. */
#include <stddef.h>
#include <stdint.h>

typedef struct pa_simple pa_simple;

/* PulseAudio's own numbers: PA_ERR_CONNECTIONREFUSED, PA_ERR_NOTSUPPORTED */
#define PA_ERR_CONNECTIONREFUSED 6
#define PA_ERR_NOTSUPPORTED 19

pa_simple *pa_simple_new(const char *server, const char *name, int dir, const char *dev,
                         const char *stream_name, const void *ss, const void *map,
                         const void *attr, int *error)
{
	(void)server; (void)name; (void)dir; (void)dev; (void)stream_name;
	(void)ss; (void)map; (void)attr;
	if (error) *error = PA_ERR_CONNECTIONREFUSED;
	return NULL;
}

void pa_simple_free(pa_simple *s) { (void)s; }

int pa_simple_write(pa_simple *s, const void *data, size_t bytes, int *error)
{
	(void)s; (void)data; (void)bytes;
	if (error) *error = PA_ERR_NOTSUPPORTED;
	return -1;
}

int pa_simple_read(pa_simple *s, void *data, size_t bytes, int *error)
{
	(void)s; (void)data; (void)bytes;
	if (error) *error = PA_ERR_NOTSUPPORTED;
	return -1;
}

int pa_simple_drain(pa_simple *s, int *error)
{
	(void)s;
	if (error) *error = PA_ERR_NOTSUPPORTED;
	return -1;
}

int pa_simple_flush(pa_simple *s, int *error)
{
	(void)s;
	if (error) *error = PA_ERR_NOTSUPPORTED;
	return -1;
}

uint64_t pa_simple_get_latency(pa_simple *s, int *error)
{
	(void)s;
	if (error) *error = PA_ERR_NOTSUPPORTED;
	return (uint64_t)-1;
}

/* What a caller prints when the connection fails */
const char *pa_strerror(int error)
{
	return error == PA_ERR_CONNECTIONREFUSED ? "Connection refused (no PulseAudio on this device)"
	                                         : "Not supported";
}
