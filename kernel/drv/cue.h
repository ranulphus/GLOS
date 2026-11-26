/* A CUE sheet's disc (docs/v1/native-dos.md, N4's C6 and C7: a CD image's every track, the audio ones played
 * digitally). Plain C over a callback for the files' sizes, so tests/host/cue_test.c runs it.
 *
 * Read: FILE "name" BINARY (or a name without quotes), TRACK nn MODE1/2048 | MODE1/2352 | MODE2/2352 | AUDIO,
 * INDEX 00 and 01, PREGAP, POSTGAP; REM, TITLE, PERFORMER, FLAGS, ISRC, CATALOG and the like passed over. The disc
 * is laid out as a CD's: each FILE's sectors after the previous FILE's end, a PREGAP (not in the file) before its
 * track's INDEX 00 or 01, an INDEX 00 to 01 stretch (in the file) the track's own pregap, a POSTGAP after the
 * track. A track's `start` is its INDEX 01 on the disc (the TOC's address); the sectors from `first` to `end`
 * are in its file, from sector `off` of the file at `start` (so `first` is earlier by its INDEX 00 stretch);
 * the rest of the disc is silence. */
#ifndef K_DRV_CUE_H
#define K_DRV_CUE_H

#define CUE_TRACKS 99
#define CUE_FILES  99
#define CUE_NAME   64

enum { CUE_AUDIO, CUE_MODE1_2048, CUE_MODE1_2352, CUE_MODE2_2352 };

struct cue_track {
    int number, type, file;
    unsigned bps;                       /* a sector's bytes in its file: 2048 or 2352 */
    unsigned long start;                /* its INDEX 01 on the disc */
    unsigned long first, end;           /* the disc's sectors that are in its file: first to end (excluded) */
    unsigned long off;                  /* the file's sector at start */
};

struct cue {
    int ntracks, nfiles;
    struct cue_track t[CUE_TRACKS];
    char file[CUE_FILES][CUE_NAME];
    unsigned long leadout;              /* the disc's end */
};

/* text (a sheet's, NUL-ended) into *c; size: file i's bytes (-1: none such). 0, or -1 with *why. */
int cue_parse(const char *text, long (*size)(void *ctx, const char *name), void *ctx, struct cue *c,
              const char **why);
/* The track that disc sector lba is in (its first to end, or its pregap and postgap: from the previous track's
   end to its own), or -1 past the last; *file_sector the file's sector, or -1 where the disc is silent there. */
int cue_find(const struct cue *c, unsigned long lba, long *file_sector);

#endif
