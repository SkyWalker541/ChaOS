/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_PMCAT_H
#define CHAOS_PMCAT_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: PortMaster's catalog, browsed in ChaOS's own menus.
 *
 * PortMaster publishes every port it has in one file, ports.json, with each
 * release of PortMaster-New on GitHub: a port's title, porter, description,
 * genres, what it needs (reqs, arch, runtime), whether it is free to play as
 * it comes ("ready to run") and where its zip is, with its MD5. The same file
 * lists the runtimes (Godot, .NET, Java...) under "utils". This reads it -
 * nothing here touches the network or the screen - and sorts the ports by
 * whether they can run on a Brick under ChaOS:
 *
 *   Runs on ChaOS     tried on a Brick under ChaOS and known to work
 *                     (ChaOS/ports/tested.txt, a zip name a line)
 *   May Run on ChaOS  nothing in what the port says it needs rules it out
 *   Show All          everything, the ones that won't run marked as such
 *
 * What rules a port out: a second analog stick (the Brick has none; one is
 * fine, the d-pad stands in for it), 2 GB of memory (the Brick has 1),
 * OpenGL (the Brick's GPU does GLES), "!trimui" (known not to work on
 * TrimUI's devices), or no 64-bit ARM build. "power" (wants a strong device)
 * and "exp" (experimental) are warnings, not reasons.
 *
 * The document is kept whole, in memory (2.5 MB), and each port holds spans
 * into it, so a description is read out only when asked for. */

/* Where the catalog comes from */
#define PMCAT_URL \
	"https://github.com/PortsMaster/PortMaster-New/releases/latest/download/ports.json"
/* A port's pictures, by its zip's name less .zip */
#define PMCAT_IMG_BASE \
	"https://raw.githubusercontent.com/PortsMaster/PortMaster-New/main/ports/"

enum {
	PMF_RTR       = 1 << 0,   /* ready to run: free, nothing else to copy over */
	PMF_EXP       = 1 << 1,   /* experimental */
	PMF_POWER     = 1 << 2,   /* wants a powerful device */
	PMF_NO        = 1 << 3,   /* ruled out (pm_port.why says why) */
	PMF_TESTED    = 1 << 4,   /* on ChaOS's tested list */
	PMF_INSTALLED = 1 << 5,
	PMF_UPDATE    = 1 << 6,   /* installed, and the catalog has a newer one */
	PMF_RUNTIME   = 1 << 7,   /* needs a runtime fetched with it */
};

typedef enum {
	PMS_TESTED, PMS_MAY, PMS_ALL, PMS_INSTALLED, PMS_UPDATES, PMS_COUNT
} pm_section;

typedef struct {
	char zip[64];        /* "gonefishing.zip": the catalog's key */
	char title[96];
	char dir[64];        /* its folder in Roms/Ports: "gonefishing" */
	char script[96];     /* its script there: "Gone Fishing.sh" */
	char genres[64];     /* "action, platformer" */
	char porter[64];     /* "SkyWalker541" or "A, B" */
	char shot[48];       /* its screenshot's file name, "" if none */
	const char *why;     /* PMF_NO: why not, for a person */
	unsigned flags;
	int version;         /* the catalog's */
	int have_version;    /* the installed one's port.json, PMF_INSTALLED */
	long size;           /* the zip's, in bytes */
	int added;           /* yyyymmdd it came to PortMaster, 0 if unknown */
	const char *attr_b, *attr_e;   /* attr, in the document */
	const char *src_b, *src_e;     /* source */
} pm_port;

typedef struct {
	char *doc;
	size_t len;
	pm_port *p;
	int n;
	const char *utils_b, *utils_e;
} pm_catalog;

/* Read ports.json at `path`. `ports_dir` is Roms/Ports, to see what is
 * installed (each port's own <dir>/port.json); `tested` is ChaOS's list.
 * Either may be NULL. False if the file is missing or isn't the catalog. */
bool pmcat_load(pm_catalog *c, const char *path, const char *ports_dir, const char *tested);
void pmcat_free(pm_catalog *c);
/* Look at Roms/Ports again, after an install or a removal */
void pmcat_installed(pm_catalog *c, const char *ports_dir);

/* Whether a port belongs in a section */
bool pmcat_in(const pm_port *p, pm_section s);

/* The ports in `s` (and of `genre`, when not NULL or ""), ready to run first,
 * then by title. Writes up to `max` indices; returns how many there are. */
int pmcat_list(const pm_catalog *c, pm_section s, const char *genre, int *out, int max);

/* The genres there are, as written, in order of how many ports have each.
 * Returns how many; `out` gets up to `max` of them. */
int pmcat_genres(const pm_catalog *c, char out[][24], int max);

/* A text field of a port's attr ("desc", "inst"), unescaped */
bool pmcat_text(const pm_port *p, const char *field, char *out, size_t n);
/* Its zip: where from, and the MD5 it must have ("" when there is none) */
bool pmcat_source(const pm_port *p, char *url, size_t un, char *md5, size_t mn);
/* The runtimes it needs, by the names its script looks for in
 * PortMaster/libs ("godot_4.3.squashfs"). Returns how many. */
int pmcat_runtimes(const pm_port *p, char out[][64], int max);
/* Where a runtime comes from: its aarch64 build in the catalog's utils */
bool pmcat_runtime(const pm_catalog *c, const char *name, char *url, size_t un,
                   char *md5, size_t mn, long *size);
/* Its screenshot's URL and the file name to keep it under ("<zip less
 * .zip>.png"); false when the port has none */
bool pmcat_shot(const pm_port *p, char *url, size_t un, char *file, size_t fn);

/* ---- what an install put on the card ----------------------------------- *
 *
 * Each install keeps the list of what its zip unpacked (its manifest), so a
 * removal can take the port's own files and leave what it made as it ran -
 * saves, settings - and what you copied in: reinstalled, it carries on. */

/* The names in a zip, one a line, into `out` (a manifest). False if it isn't
 * a zip this can read (zip64, damaged). */
bool pmcat_zip_names(const char *zip, const char *out);
/* Removes, under `ports_dir`, the files a manifest names, then each folder
 * it names that is left empty. Nothing outside ports_dir, nothing named
 * with "..". Returns how many files went. */
int pmcat_manifest_remove(const char *ports_dir, const char *manifest);

/* By the catalog's key; -1 if not there */
int pmcat_find(const pm_catalog *c, const char *zip);

#endif
