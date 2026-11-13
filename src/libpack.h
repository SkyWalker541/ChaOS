/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_LIBPACK_H
#define CHAOS_LIBPACK_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: the libraries a port needs that the Brick hasn't got, and ChaOS's
 * pack of them.
 *
 * PortMaster's catalog says which runtimes a port needs (pmcat_runtimes),
 * but not which system libraries: a port takes for granted the ones the
 * usual firmwares have. Some of those the Brick's root has, ChaOS adds the
 * common ones (tools/build-portlibs.sh), and the rest are in a pack on
 * ChaOS's GitHub, a library a file beside a list of them (libpack.json,
 * tools/build-libpack.sh), so one can be added without a ChaOS release.
 *
 * This reads a port's programs for what they load (each ELF's DT_NEEDED,
 * followed through the libraries the port carries), says which of those
 * nothing on the device has, and looks them up in the pack's list. The
 * fetching is the launcher's (main.c), with its panel. No SDL here: `make
 * check-libpack` runs it on a computer. */

#define LIBPACK_BASE "https://github.com/SkyWalker541/ChaOS/releases/download/portlibs/"
#define LIBPACK_LIST LIBPACK_BASE "libpack.json"
#define LIBPACK_NAME 64

/* An ELF's needed libraries (and its own soname, "" if it has none). False
 * if it isn't a 64-bit little-endian ELF. Writes up to `max`. */
bool libpack_elf(const char *path, char needed[][LIBPACK_NAME], int max, int *n,
                 char *soname, size_t sn);

/* What the programs under `port_dir` need that neither the port nor any of
 * `have_dirs` (NULL-terminated: /lib, /usr/lib, ChaOS's) has. Programs are
 * the executables; a library the port carries counts only when a program
 * (or a library it loads) does. Returns how many, writing up to `max`. */
int libpack_missing(const char *port_dir, const char *const *have_dirs,
                    char out[][LIBPACK_NAME], int max);

/* A library in the pack's list: its file there, MD5 and size. */
bool libpack_find(const char *list_json, size_t len, const char *name,
                  char *file, size_t fn, char *md5, size_t mn, long *size);

#endif
