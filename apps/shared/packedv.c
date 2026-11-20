/* stb_vorbis itself (third_party/stb/stb_vorbis.c, unchanged), built in a file of its own: its names and
   minimp3's would meet in packed.c, which is what uses it. What a compiler has to say of it is its own. */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wshift-negative-value"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define STB_VORBIS_NO_PUSHDATA_API
#ifndef NDEBUG
#define NDEBUG                          /* (its assertions: a file that fails one is a damaged file, not an abort) */
#endif
#include "../../third_party/stb/stb_vorbis.c"
