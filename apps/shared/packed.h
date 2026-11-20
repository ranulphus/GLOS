/* Packed sound, unpacked as it is wanted: MP3 (and the MPEG layers before it) and Ogg Vorbis files, for the
   Player (phase F; docs/v1/apps/audio-player.md). The decoders are others' work, unchanged (third_party/minimp3,
   third_party/stb's stb_vorbis: THIRD_PARTY.md); this is what stands between them and source.c. They do their
   sums in floating point: a Pentium's work, and a 486's only for the patient. */
#ifndef PACKED_H
#define PACKED_H

#include <stdio.h>

#include "source.h"

struct packed;

/* The file f, which begins with the `have` bytes at `head` (12 at least) and is `size` bytes long: opened as
   what its bytes say it is, f the reader's from then on (closed by packed_close; on a failure the caller's
   still). info is filled: rate, channels (1 or 2: more are mixed down), frames (an MP3's with no count in it:
   reckoned from its size), kind, title, detail. NULL and *err (SOURCE_E_KIND: neither kind; _DAMAGED,
   _MEMORY). */
struct packed *packed_open(FILE *f, const unsigned char *head, unsigned have, unsigned long size,
                           struct source_info *info, int *err);
void packed_close(struct packed *p);
unsigned long packed_read(struct packed *p, short *pcm, unsigned long frames);  /* those made: fewer at the end */
int packed_seek(struct packed *p, unsigned long frame);                /* 0, or -1 (an MP3's: near enough) */

#endif
