/* Bytes unpacked that LHA's -lh5-, -lh6- or -lh7- packed, or ARJ's methods 1 to 4 (lzh.c says how they are
   packed): for the LZH and ARJ archives Files goes into (apps/shared/arc.c). One code for the app and for the
   host's test (tests/host/arc_test.c). */
#ifndef LZH_H
#define LZH_H

enum { LZH_LH5 = 1, LZH_LH6, LZH_LH7, LZH_ARJ, LZH_ARJ4 };     /* (LZH_ARJ: its methods 1, 2 and 3 are one) */
#define LZH_DAMAGED (-1)                /* the bytes are not what that method makes */
#define LZH_MEMORY (-2)                 /* no memory for its tables and window (some 75 KB) */

/* `packed` bytes, had one at a time from get (-1: there are no more), unpacked to `size` bytes, given to put in
   pieces as they are made (anything but 0 from it ends the work, and is what this returns). 0: all of it. */
int lzh_unpack(int method, unsigned long packed, unsigned long size, int (*get)(void *ctx),
               int (*put)(void *ctx, const void *p, unsigned n), void *ctx);

#endif
