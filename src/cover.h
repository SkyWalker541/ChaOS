/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_COVER_H
#define CHAOS_COVER_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: a game's cover from ChaOS Transfer's page (hare_set_cover). `rom` is
 * the game, a file directly in a console's folder under P_ROMS. The picture
 * is `file` when the phone sent one, else what `url` leads to: a picture's
 * own address, a Google Images result (its imgurl=), or an itch.io game's
 * page (the cover its data.json names). It must decode as a picture; it is
 * written as <console>/.media/<stem>.png, beside the old one and renamed over
 * it once whole, and `out` says where. False, with why, for anything else.
 * Waits for the network; the transfer screen's request is what calls it. */
bool cover_from_web(const char *rom, const char *url, const char *file,
                    char *out, size_t on, char *why, size_t wn);

#endif
