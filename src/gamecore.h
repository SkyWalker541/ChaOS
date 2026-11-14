/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_GAMECORE_H
#define CHAOS_GAMECORE_H

/* ChaOS: core options for one game, sent after its folder's (platform.c's
 * coreopt.<tag>.*), so they win.
 *
 * THE MACHINE A GAME BOY GAME PLAYS ON. Game Boy and Game Boy Color are one
 * core here (mGBA), and their folders take each other's files (systems.cfg),
 * because homebrew - GB Studio's especially - names them as it likes. So the
 * cartridge decides, as it does on real hardware: header byte 0x143 with its
 * top bit set (0x80, made for the Color; 0xC0, made only for it) plays on a
 * Game Boy Color, in its own colors, and anything else plays on a Game Boy, as
 * it was meant to - never colorized by a Color's or a Super Game Boy's
 * palettes. The folder still decides the shelf's settings, shader, overlay and
 * buttons. A .zip is looked into: its first Game Boy file, inflated as far as
 * the header.
 *
 * PT SKYWALKER541'S CORE SETTINGS. The shader is tuned with the emulator set
 * a certain way (its README's "Emulator core settings", for Gambatte and
 * SameBoy on the Game Boys and mGBA on the GBA; here all mGBA), by the
 * shader's own Game System:
 *
 *     1 Game Boy / Pocket   color correction off, interframe blending off,
 *                           and no colorization: the grayscale palette
 *     2 Game Boy Color      color correction GBC, blending off
 *     3 GBA SP              color correction GBA, blending Smart
 *     4 GBA (original)      color correction GBA, blending LCD ghosting
 *     0 Manual              nothing set
 *
 * With PT not in use (off, another shader, its lcd3x style) the same options
 * are stated at mGBA's own defaults - off, off, and the folder's palette - so
 * a game changed in the in-game menu goes back as it should.
 *
 * None of these needs a restart: mGBA reads color correction, blending and
 * the palette again whenever an option changes (its retro_run, built 16-bit
 * 5:6:5 as ChaOS's is), so a change in the in-game menu shows at once. Only
 * the machine (mgba_gb_model) is read at load, and that never changes while
 * a game plays. */

#define GC_MAX 6
#define GC_LEN 64

/* PT's Game System when PT SkyWalker541 is the shader in use, else -1 */
int gc_options(const char *tag, const char *rom, int pt_system, char out[GC_MAX][GC_LEN]);

/* The cartridge's header byte 0x143 (its Color flag), read from the file or
 * from inside a .zip; -1 if it can't be read */
int gc_cgb_flag(const char *rom);

#endif
