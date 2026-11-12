/* A deflate stream (RFC 1951, as zlib and gzip make it, without their wrappers) unpacked through a 32 KB window:
 * the loader's, for a packed GLOSK.BIN (loader/main.c; tools/packkernel.py), and built for the host by its test
 * (tests/host/inflate_test.c). No memory of its own but its tables (about 3 KB, static). */
#ifndef GLOS_INFLATE_H
#define GLOS_INFLATE_H

#ifndef INFL_FAR
#define INFL_FAR                        /* (the loader: far, the window is a DOS block of its own) */
#endif
#define INFL_WINDOW 32768u

/* More of the stream: *p the bytes, their count returned (0: there is no more). */
typedef unsigned (*infl_fill)(const unsigned char **p, void *ctx);
/* The window's first n bytes are the next n bytes unpacked (n is INFL_WINDOW until the last piece): 0, or
   anything else to stop. */
typedef int (*infl_flush)(unsigned n, void *ctx);

/* 0: the whole stream unpacked. -1: the stream is damaged or ends too soon; -2: flush said stop. */
int inflate_stream(infl_fill fill, infl_flush flush, void *ctx, unsigned char INFL_FAR *window);

#endif
