/* 7z archives for apps/shared/arc.c (not for apps: theirs is arc.h): the list and the files' bytes, read by the
   LZMA SDK's own reader (third_party/lzma). */
#ifndef ARC7Z_H
#define ARC7Z_H

#include <stdio.h>

struct sz;

/* The archive's list read from f (at any place). Each of its files and folders is told to `entry` in the
   archive's order: its number there, its name (UTF-8, '/' between the parts, n bytes), whether it is a folder,
   its size, its time (Unix's; 0: none), and ARC_OK or why its bytes cannot be had (ARC_E_LOCKED, ARC_E_METHOD,
   ARC_E_BIG). entry: 0, or an ARC_ error that ends it. ARC_OK and *out, or an ARC_ error. */
int sz_open(FILE *f, struct sz **out,
            int (*entry)(void *ctx, unsigned i, const char *name, unsigned n, int dir, unsigned long size,
                         unsigned long when, int how),
            void *ctx);

/* File i's bytes to data, in pieces (anything but 0 from it: ARC_E_STOP). The solid block it is in is unpacked
   whole into memory and kept for the next call (sz_drop gives it back). */
int sz_extract(struct sz *z, FILE *f, unsigned i, int (*data)(void *ctx, const void *p, unsigned n), void *ctx);
unsigned long sz_kept(const struct sz *z);      /* bytes of the block kept */
void sz_drop(struct sz *z);
void sz_close(struct sz *z);

#endif
