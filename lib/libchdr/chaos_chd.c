/* ChaOS: libchdr, built as one file, for reading .chd discs - RetroAchievements
 * hashes a PlayStation disc by the program on it (src/rahash_disc.c). libchdr's
 * own unity.c, without miniz: zlib is the system's (CHDR_SYSTEM_ZLIB), which
 * the launcher links already. libchdr is BSD-3-Clause (LICENSE.txt), LZMA SDK
 * public domain, Zstandard BSD. From libretro/pcsx_rearmed 7b0e8f8f's
 * deps/libchdr, unchanged but for the folder names in four #includes. */
#include "deps/lzma/src/LzmaDec.c"
#include "deps/zstd/zstddeclib.c"

#include "src/libchdr_bitstream.c"
#include "src/libchdr_cdrom.c"
#include "src/libchdr_chd.c"
#include "src/libchdr_codec_cdfl.c"
#include "src/libchdr_codec_cdlz.c"
#include "src/libchdr_codec_cdzl.c"
#include "src/libchdr_codec_cdzs.c"
#include "src/libchdr_codec_flac.c"
#include "src/libchdr_codec_huff.c"
#include "src/libchdr_codec_lzma.c"
#include "src/libchdr_codec_zlib.c"
#include "src/libchdr_codec_zstd.c"
#include "src/libchdr_flac.c"
#include "src/libchdr_huffman.c"
