/* kernel/drv/cue.c on the host: CUE sheets laid out as discs (N4's C6, an image's audio tracks). mkiso.py's own
 * sheet (a data file, then a file a tone track with a PREGAP each); a one-file rip whose tracks have INDEX 00
 * pregaps in the file; a PREGAP inside one file (silence, the file's next sectors after it); a POSTGAP; CR LF,
 * REM lines, lower case and a name with no quotes; the sheets refused. */
#include <stdio.h>
#include <string.h>

#include "../../kernel/drv/cue.h"

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("cue_test: FAIL %s\n", what);
        fails++;
    }
}

static const char *names[4];
static long sizes[4];

static long size(void *ctx, const char *name)
{
    int k;
    (void)ctx;
    for (k = 0; k < 4 && names[k]; k++)
        if (!strcmp(names[k], name))
            return sizes[k];
    return -1;
}

static void files(const char *a, long sa, const char *b, long sb, const char *c, long sc)
{
    names[0] = a, sizes[0] = sa, names[1] = b, sizes[1] = sb, names[2] = c, sizes[2] = sc, names[3] = 0;
}

static int at(const struct cue *c, unsigned long lba, int track, long fs)
{
    long got;
    int k = cue_find(c, lba, &got);
    return k == track && got == fs;
}

int main(void)
{
    static struct cue c;
    const char *why = "";
    int r;

    /* mkiso.py --cue --tone 8 --tone 4 (744 data sectors) */
    files("disc.iso", 744L * 2048, "disc-02.bin", 600L * 2352, "disc-03.bin", 300L * 2352);
    r = cue_parse("FILE \"disc.iso\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n"
                  "FILE \"disc-02.bin\" BINARY\n  TRACK 02 AUDIO\n    PREGAP 00:02:00\n    INDEX 01 00:00:00\n"
                  "FILE \"disc-03.bin\" BINARY\n  TRACK 03 AUDIO\n    PREGAP 00:02:00\n    INDEX 01 00:00:00\n",
                  size, 0, &c, &why);
    check(r == 0 && c.ntracks == 3 && c.nfiles == 3, "mkiso's sheet read");
    check(c.t[0].type == CUE_MODE1_2048 && c.t[0].bps == 2048 && c.t[0].start == 0 && c.t[0].end == 744,
          "its data track: 0 to 744");
    check(c.t[1].type == CUE_AUDIO && c.t[1].start == 894 && c.t[1].first == 894 && c.t[1].end == 1494
          && c.t[1].file == 1, "track 2 after its two seconds' pregap: 894 to 1494, its own file");
    check(c.t[2].start == 1644 && c.t[2].end == 1944 && c.leadout == 1944, "track 3 at 1644; the lead-out 1944");
    check(at(&c, 800, 1, -1) && at(&c, 894, 1, 0) && at(&c, 1493, 1, 599) && at(&c, 1644, 2, 0),
          "sectors found: the pregap silent, the file's first and last");
    check(cue_find(&c, 1944, &(long){ 0 }) == -1, "nothing past the lead-out");

    /* one file, INDEX 00 pregaps in it */
    files("game.bin", 13500L * 2352, 0, 0, 0, 0);
    r = cue_parse("REM a rip\r\nFILE \"game.bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n"
                  "  TRACK 02 AUDIO\r\n    FLAGS DCP\r\n    INDEX 00 01:00:00\r\n    INDEX 01 01:02:00\r\n"
                  "  TRACK 03 AUDIO\r\n    INDEX 00 02:00:00\r\n    INDEX 01 02:02:00\r\n", size, 0, &c, &why);
    check(r == 0 && c.ntracks == 3 && c.nfiles == 1, "a one-file rip read (CR LF, REM, FLAGS)");
    check(c.t[0].bps == 2352 && c.t[0].end == 4500, "its data track ends where track 2's INDEX 00 begins");
    check(c.t[1].first == 4500 && c.t[1].start == 4650 && c.t[1].off == 4650 && c.t[1].end == 9000,
          "track 2: its pregap in the file, 4500; INDEX 01 at 4650");
    check(c.t[2].start == 9150 && c.leadout == 13500, "track 3 at 9150; the lead-out the file's end");
    check(at(&c, 4500, 1, 4500) && at(&c, 9149, 2, 9149) && at(&c, 13499, 2, 13499), "its sectors at the file's own");

    /* a PREGAP inside one file: silence, then the file's next sectors */
    files("a.bin", 1500L * 2352, 0, 0, 0, 0);
    r = cue_parse("file a.bin binary\n track 01 audio\n  index 01 00:00:00\n track 02 audio\n  pregap 00:02:00\n"
                  "  index 01 00:10:00\n", size, 0, &c, &why);
    check(r == 0 && c.ntracks == 2, "lower case, a name with no quotes");
    check(c.t[0].end == 750 && c.t[1].first == 900 && c.t[1].start == 900 && c.t[1].off == 750
          && c.t[1].end == 1650 && c.leadout == 1650, "the PREGAP between: 750 to 900, then the file from 750");
    check(at(&c, 749, 0, 749) && at(&c, 760, 1, -1) && at(&c, 900, 1, 750) && at(&c, 1649, 1, 1499),
          "the gap silent, the file's sectors either side");

    /* a POSTGAP before the next file */
    files("d.bin", 100L * 2352, "e.bin", 200L * 2352, 0, 0);
    r = cue_parse("FILE d.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nPOSTGAP 00:01:00\n"
                  "FILE e.bin BINARY\nTRACK 02 AUDIO\nINDEX 01 00:00:00\n", size, 0, &c, &why);
    check(r == 0 && c.t[0].end == 100 && c.t[1].start == 175 && c.leadout == 375, "a POSTGAP: 75 sectors between");
    check(at(&c, 120, 1, -1), "... silent");

    /* refused */
    files("x.bin", 1000L * 2352, 0, 0, 0, 0);
    check(cue_parse("TRACK 01 AUDIO\nINDEX 01 00:00:00\n", size, 0, &c, &why) != 0, "a TRACK before any FILE");
    check(cue_parse("FILE \"y.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n", size, 0, &c, &why) != 0,
          "a FILE that isn't there");
    check(cue_parse("FILE \"x.bin\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n", size, 0, &c, &why) != 0,
          "a WAVE file");
    check(cue_parse("FILE \"x.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 00 00:00:00\n", size, 0, &c, &why) != 0,
          "a TRACK with no INDEX 01");
    check(cue_parse("FILE \"x.bin\" BINARY\nTRACK 02 AUDIO\nINDEX 01 00:00:00\nTRACK 01 AUDIO\nINDEX 01 00:01:00\n",
                    size, 0, &c, &why) != 0, "tracks out of order");
    check(cue_parse("FILE \"x.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:61:00\n", size, 0, &c, &why) != 0,
          "an INDEX of 61 seconds");
    check(cue_parse("FILE \"x.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 01:00:00\n", size, 0, &c, &why) != 0,
          "an INDEX past the file's end");
    check(cue_parse("", size, 0, &c, &why) != 0, "no TRACK");

    printf("cue_test: %d failures\n", fails);
    return fails != 0;
}
