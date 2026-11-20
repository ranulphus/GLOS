/* Packed sound, unpacked as it is wanted (packed.h): MP3 by minimp3, Ogg Vorbis by stb_vorbis. */
#include <stdlib.h>
#include <string.h>

#include "packed.h"

/* (The decoders are others' work, unchanged: what a compiler has to say of them is theirs.) */
#pragma GCC diagnostic push
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
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD                 /* (a 486's and a Pentium's sums, and the same samples on the host) */
#include "../../third_party/minimp3/minimp3.h"

#pragma GCC diagnostic pop

#define STB_VORBIS_HEADER_ONLY          /* (the decoder itself is packedv.c: its names and minimp3's would meet here) */
#define STB_VORBIS_NO_PUSHDATA_API
#include "../../third_party/stb/stb_vorbis.c"

#define MP3_BUF 16384                   /* the file's bytes in hand: several frames (one is 1441 at most) */
#define PROBE (128ul * 1024)            /* how far into a file its first frame is looked for */

struct packed {
    FILE *f;
    stb_vorbis *ogg;                    /* Ogg Vorbis, or ... */
    /* ... MP3: */
    mp3dec_t dec;
    unsigned char buf[MP3_BUF];
    unsigned have, at;                  /* bytes of the file in buf, and how many of them are done with */
    unsigned long start, end;           /* where its frames begin and end in the file */
    int eof;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    unsigned ready, given;              /* samples (of all channels) in pcm, and those handed on */
    unsigned channels, per_frame;       /* per_frame: a frame's samples a channel (1152, or 576 at the low rates) */
    unsigned long frames;               /* the whole length, as told or reckoned */
};

static void tidy(char *out, size_t room, const char *in, size_t n)      /* a tag's text: what can be shown */
{
    size_t i, k = 0;
    for (i = 0; i < n && in[i] && k + 1 < room; i++)
        out[k++] = (unsigned char)in[i] >= 32 && (unsigned char)in[i] < 127 ? in[i] : ' ';
    while (k && out[k - 1] == ' ')
        k--;
    out[k] = 0;
}

/* ---- MP3 */

static void fill(struct packed *p)      /* the buffer's bytes not yet used moved down, and more read after them */
{
    unsigned long left;
    size_t k;
    if (p->at) {
        memmove(p->buf, p->buf + p->at, p->have - p->at);
        p->have -= p->at;
        p->at = 0;
    }
    if (p->eof || p->have == MP3_BUF)
        return;
    left = p->end - (unsigned long)ftell(p->f);
    k = MP3_BUF - p->have;
    if (k > left)
        k = (size_t)left;
    if (!k || (k = fread(p->buf + p->have, 1, k, p->f)) == 0)
        p->eof = 1;
    p->have += (unsigned)k;
}

/* The next frame into pcm (NULL: passed over, its header alone read): its samples a channel, 0 at the end. */
static unsigned next_frame(struct packed *p, short *pcm, mp3dec_frame_info_t *info)
{
    for (;;) {
        int n;
        if (p->have - p->at < 4096 && !p->eof)
            fill(p);
        if (p->have == p->at)
            return 0;
        n = mp3dec_decode_frame(&p->dec, p->buf + p->at, (int)(p->have - p->at), pcm, info);
        if (!info->frame_bytes) {               /* nothing of a frame in what is left */
            if (p->eof)
                return 0;
            p->at = p->have;                    /* (rubbish to the buffer's end: on with the next) */
            continue;
        }
        p->at += (unsigned)info->frame_bytes;
        if (n > 0)
            return (unsigned)n;
    }
}

/* The count of frames a "Xing" or "Info" header in the first frame gives (LAME's and others' way of saying how
   long a file of varying frames is): 0 if it has none. frame: the first frame's bytes. */
static unsigned long xing_frames(const unsigned char *frame, unsigned n, int mpeg1, int mono)
{
    unsigned at = 4 + (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
    if (at + 12 > n || (memcmp(frame + at, "Xing", 4) && memcmp(frame + at, "Info", 4)))
        return 0;
    if (!(frame[at + 7] & 1))
        return 0;
    return (unsigned long)frame[at + 8] << 24 | (unsigned long)frame[at + 9] << 16 | (unsigned long)frame[at + 10] << 8
           | frame[at + 11];
}

static int mp3_open(struct packed *p, unsigned long size, struct source_info *info)
{
    mp3dec_frame_info_t fi;
    unsigned char tag[128];
    unsigned long count;
    unsigned n, first;
    p->start = 0, p->end = size;
    if (size > 128 && fseek(p->f, (long)size - 128, SEEK_SET) == 0 && fread(tag, 1, 128, p->f) == 128
        && !memcmp(tag, "TAG", 3)) {            /* the old tag at its end: the song's name, and not sound */
        tidy(info->title, sizeof info->title, (const char *)tag + 3, 30);
        p->end = size - 128;
    }
    if (fseek(p->f, 0, SEEK_SET) != 0 || fread(tag, 1, 10, p->f) != 10)
        return SOURCE_E_KIND;
    if (!memcmp(tag, "ID3", 3) && !((tag[6] | tag[7] | tag[8] | tag[9]) & 0x80)) {      /* the new tag at its start */
        p->start = 10 + ((unsigned long)tag[6] << 21 | (unsigned long)tag[7] << 14 | (unsigned long)tag[8] << 7 | tag[9]);
        if (p->start >= p->end)
            return SOURCE_E_DAMAGED;
    }
    if (fseek(p->f, (long)p->start, SEEK_SET) != 0)
        return SOURCE_E_KIND;
    mp3dec_init(&p->dec);
    /* Its first frame, looked for no further than PROBE in: a file that has none there is no MP3. */
    for (;;) {
        unsigned long where;
        if (p->have - p->at < 4096 && !p->eof)
            fill(p);
        where = (unsigned long)ftell(p->f) - (p->have - p->at);
        if (p->have == p->at || where - p->start > PROBE)
            return SOURCE_E_KIND;
        first = p->at;
        n = (unsigned)mp3dec_decode_frame(&p->dec, p->buf + p->at, (int)(p->have - p->at), NULL, &fi);
        if (!fi.frame_bytes) {
            if (p->eof)
                return SOURCE_E_KIND;
            p->at = p->have > 4 ? p->have - 4 : p->have;        /* (a header may straddle the buffer's end) */
            continue;
        }
        if (n) {                                /* a frame: the file's sound begins with it */
            first = p->at + (unsigned)fi.frame_offset;
            p->start = where + (unsigned)fi.frame_offset;
            break;
        }
        p->at += (unsigned)fi.frame_bytes;      /* (passed over: a tag, rubbish) */
    }
    if (fi.channels < 1 || fi.channels > 2 || fi.hz < 8000 || fi.hz > 48000)
        return SOURCE_E_DAMAGED;
    p->channels = (unsigned)fi.channels, p->per_frame = n;
    info->rate = (unsigned)fi.hz, info->channels = p->channels;
    count = xing_frames(p->buf + first, p->have - first, fi.hz >= 32000, fi.channels == 1);
    if (count) {
        p->frames = count * n;
    } else {                                    /* frames all of one size, as far as anyone can tell */
        unsigned bytes = (unsigned)fi.frame_bytes - (unsigned)fi.frame_offset;
        p->frames = bytes ? (p->end - p->start) / bytes * n : 0;
    }
    info->frames = p->frames;
    snprintf(info->kind, sizeof info->kind, "%s", fi.layer == 3 ? "MP3" : fi.layer == 2 ? "MP2" : "MP1");
    snprintf(info->detail, sizeof info->detail, "%d kbit/s%s", fi.bitrate_kbps, count ? " (varying)" : "");
    /* From the first frame again, decoded this time. */
    if (fseek(p->f, (long)p->start, SEEK_SET) != 0)
        return SOURCE_E_OPEN;
    p->have = p->at = 0, p->eof = 0;
    mp3dec_init(&p->dec);
    return SOURCE_OK;
}

static unsigned long mp3_read(struct packed *p, short *pcm, unsigned long frames)
{
    unsigned long done = 0;
    while (done < frames) {
        unsigned k;
        if (p->given == p->ready) {
            mp3dec_frame_info_t fi;
            unsigned n = next_frame(p, p->pcm, &fi);
            if (!n)
                break;
            if ((unsigned)fi.channels != p->channels) {         /* (a file that changes its mind: as it began) */
                unsigned i;
                if (p->channels == 2)
                    for (i = n; i-- > 0; )
                        p->pcm[2 * i] = p->pcm[2 * i + 1] = p->pcm[i];
                else
                    for (i = 0; i < n; i++)
                        p->pcm[i] = (short)((p->pcm[2 * i] + p->pcm[2 * i + 1]) / 2);
            }
            p->ready = n * p->channels, p->given = 0;
        }
        k = (p->ready - p->given) / p->channels;
        if (k > frames - done)
            k = (unsigned)(frames - done);
        memcpy(pcm + done * p->channels, p->pcm + p->given, (size_t)k * p->channels * sizeof(short));
        p->given += k * p->channels;
        done += k;
    }
    return done;
}

/* To a frame: the place in the file that is as far through it as the frame is through the sound (right where
   the frames are all one size, near where they vary), then on from the first frame found there. */
static int mp3_seek(struct packed *p, unsigned long frame)
{
    unsigned long long span = p->end - p->start;
    unsigned long where = p->start + (p->frames ? (unsigned long)(span * frame / p->frames) : 0);
    mp3dec_frame_info_t fi;
    if (where > p->end)
        where = p->end;
    if (fseek(p->f, (long)where, SEEK_SET) != 0)
        return -1;
    p->have = p->at = 0, p->eof = 0, p->ready = p->given = 0;
    mp3dec_init(&p->dec);
    if (frame)                                  /* (a frame leans on the one before: the first found is let go) */
        next_frame(p, p->pcm, &fi);
    return 0;
}

/* ---- both */

struct packed *packed_open(FILE *f, const unsigned char *head, unsigned have, unsigned long size,
                           struct source_info *info, int *err)
{
    struct packed *p = calloc(1, sizeof *p);
    int dummy, e;
    if (!err)
        err = &dummy;
    *err = SOURCE_E_MEMORY;
    if (!p)
        return NULL;
    p->f = f;
    if (have >= 4 && !memcmp(head, "OggS", 4)) {
        stb_vorbis_info vi;
        stb_vorbis_comment vc;
        int i, verr = 0;
        if (fseek(f, 0, SEEK_SET) != 0 || !(p->ogg = stb_vorbis_open_file(f, 0, &verr, NULL))) {
            *err = verr == VORBIS_outofmem ? SOURCE_E_MEMORY
                   : verr == VORBIS_missing_capture_pattern || verr == VORBIS_invalid_first_page ? SOURCE_E_FORMAT
                   : SOURCE_E_DAMAGED;          /* (an Ogg file of something that is no Vorbis: FORMAT) */
            free(p);
            return NULL;
        }
        vi = stb_vorbis_get_info(p->ogg);
        if (vi.channels < 1 || vi.sample_rate < 4000 || vi.sample_rate > 96000) {
            stb_vorbis_close(p->ogg);
            free(p);
            *err = SOURCE_E_DAMAGED;
            return NULL;
        }
        p->channels = vi.channels > 2 ? 2 : (unsigned)vi.channels;
        info->rate = vi.sample_rate, info->channels = p->channels;
        info->frames = p->frames = stb_vorbis_stream_length_in_samples(p->ogg);
        strcpy(info->kind, "OGG");
        snprintf(info->detail, sizeof info->detail, "Vorbis, %d channel%s", vi.channels, vi.channels == 1 ? "" : "s");
        vc = stb_vorbis_get_comment(p->ogg);
        for (i = 0; i < vc.comment_list_length; i++) {
            const char *c = vc.comment_list[i];
            if (c && strlen(c) > 6 && (c[0] == 'T' || c[0] == 't') && (c[1] == 'I' || c[1] == 'i') && (c[2] == 'T' || c[2] == 't')
                && (c[3] == 'L' || c[3] == 'l') && (c[4] == 'E' || c[4] == 'e') && c[5] == '=')
                tidy(info->title, sizeof info->title, c + 6, strlen(c + 6));
        }
        *err = SOURCE_OK;
        return p;
    }
    if ((e = mp3_open(p, size, info)) != SOURCE_OK) {
        free(p);
        *err = e;
        return NULL;
    }
    *err = SOURCE_OK;
    return p;
}

void packed_close(struct packed *p)
{
    if (!p)
        return;
    if (p->ogg)
        stb_vorbis_close(p->ogg);
    if (p->f)
        fclose(p->f);
    free(p);
}

unsigned long packed_read(struct packed *p, short *pcm, unsigned long frames)
{
    unsigned long done = 0;
    if (!p->ogg)
        return mp3_read(p, pcm, frames);
    while (done < frames) {                     /* (a call gives what one of its packets holds, or less) */
        int n = stb_vorbis_get_samples_short_interleaved(p->ogg, (int)p->channels, pcm + done * p->channels,
                                                         (int)((frames - done) * p->channels));
        if (n <= 0)
            break;
        done += (unsigned long)n;
    }
    return done;
}

int packed_seek(struct packed *p, unsigned long frame)
{
    if (p->ogg)
        return stb_vorbis_seek(p->ogg, (unsigned int)frame) ? 0 : -1;
    return mp3_seek(p, frame);
}
