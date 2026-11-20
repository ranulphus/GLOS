/* Sound as samples in memory, its edits, and WAV files read and written (wave.h): the Sound Recorder's takes and
 * the Player's WAV files (phase F).
 *
 * WAV: a RIFF file of chunks; "fmt " says how the samples are kept, "data" has them. Read: PCM of 8 bits (unsigned),
 * 16, 24 and 32 (the top 16 of them kept), one or two channels, and IMA ADPCM as Microsoft's WAVs have it (4 bits a
 * sample, in blocks that each begin with a whole sample and the step's index for every channel). Sizes that say
 * more than the file has are cut to the file (recorders that were stopped hard leave such). Written: PCM of 16
 * or 8 bits, or IMA ADPCM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wave.h"

static void *open_plain(const char *path, const char *mode) { return fopen(path, mode); }
static void *(*file_open)(const char *path, const char *mode) = open_plain;
static int (*file_unlink)(const char *path) = remove;

void wave_files(void *(*open)(const char *path, const char *mode), int (*unlink_)(const char *path))
{
    file_open = open ? open : open_plain;
    file_unlink = unlink_ ? unlink_ : remove;
}

#define MAX_SAMPLES 0x1FFFFFFFul        /* (their bytes fit 32 bits' sizes) */

static short clip(long v)
{
    return (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static void span(const struct wave *w, unsigned long *from, unsigned long *to)
{
    if (*to > w->frames)
        *to = w->frames;
    if (*from > *to)
        *from = *to;
}

static int room_for(struct wave *w, unsigned long frames)
{
    unsigned long room;
    short *p;
    if (frames <= w->room)
        return WAVE_OK;
    if (frames > MAX_SAMPLES / w->channels)
        return WAVE_E_MEMORY;
    room = w->room + w->room / 2 + 4096;        /* (half as much again: appended to block by block) */
    if (room < frames || room > MAX_SAMPLES / w->channels)
        room = frames;
    if (!(p = realloc(w->pcm, (size_t)room * w->channels * sizeof *p)))
        return WAVE_E_MEMORY;
    w->pcm = p, w->room = room;
    return WAVE_OK;
}

int wave_init(struct wave *w, unsigned rate, unsigned channels)
{
    memset(w, 0, sizeof *w);
    if (rate < 1000 || rate > 192000 || channels < 1 || channels > 2)
        return WAVE_E_ARGUMENT;
    w->rate = rate, w->channels = channels;
    return WAVE_OK;
}

void wave_free(struct wave *w)
{
    free(w->pcm);
    w->pcm = NULL;
    w->frames = w->room = 0;
}

int wave_append(struct wave *w, const short *pcm, unsigned long frames)
{
    int r = room_for(w, w->frames + frames);
    if (r != WAVE_OK || !frames)
        return r;
    memcpy(w->pcm + w->frames * w->channels, pcm, (size_t)frames * w->channels * sizeof *pcm);
    w->frames += frames;
    return WAVE_OK;
}

int wave_copy(struct wave *out, const struct wave *w, unsigned long from, unsigned long to)
{
    int r;
    span(w, &from, &to);
    if ((r = wave_init(out, w->rate, w->channels)) != WAVE_OK)
        return r;
    if (to > from && (r = wave_append(out, w->pcm + from * w->channels, to - from)) != WAVE_OK)
        wave_free(out);
    return r;
}

/* ---- edits */

int wave_cut(struct wave *w, unsigned long from, unsigned long to, struct wave *cut)
{
    int r;
    span(w, &from, &to);
    if (cut && (r = wave_copy(cut, w, from, to)) != WAVE_OK)
        return r;
    if (to == from)
        return WAVE_OK;
    memmove(w->pcm + from * w->channels, w->pcm + to * w->channels, (size_t)(w->frames - to) * w->channels * sizeof *w->pcm);
    w->frames -= to - from;
    return WAVE_OK;
}

int wave_trim(struct wave *w, unsigned long from, unsigned long to)
{
    span(w, &from, &to);
    if (to > from)
        memmove(w->pcm, w->pcm + from * w->channels, (size_t)(to - from) * w->channels * sizeof *w->pcm);
    w->frames = to - from;
    return WAVE_OK;
}

int wave_insert(struct wave *w, unsigned long at, const struct wave *piece)
{
    int r;
    if (piece->rate != w->rate || piece->channels != w->channels)
        return WAVE_E_ARGUMENT;
    if (at > w->frames)
        at = w->frames;
    if (!piece->frames)
        return WAVE_OK;
    if ((r = room_for(w, w->frames + piece->frames)) != WAVE_OK)
        return r;
    memmove(w->pcm + (at + piece->frames) * w->channels, w->pcm + at * w->channels,
            (size_t)(w->frames - at) * w->channels * sizeof *w->pcm);
    memcpy(w->pcm + at * w->channels, piece->pcm, (size_t)piece->frames * w->channels * sizeof *w->pcm);
    w->frames += piece->frames;
    return WAVE_OK;
}

void wave_gain(struct wave *w, unsigned long from, unsigned long to, int permille)
{
    unsigned long i;
    span(w, &from, &to);
    for (i = from * w->channels; i < to * w->channels; i++)
        w->pcm[i] = clip((long)w->pcm[i] * permille / 1000);
}

void wave_fade(struct wave *w, unsigned long from, unsigned long to, int in)
{
    unsigned long i, n;
    unsigned c;
    span(w, &from, &to);
    n = to - from;
    for (i = 0; i < n; i++) {
        unsigned long k = in ? i : n - 1 - i;           /* of n: the first frame of a fade in is nothing */
        for (c = 0; c < w->channels; c++) {
            short *s = &w->pcm[(from + i) * w->channels + c];
            *s = (short)((long long)*s * (long long)k / (long long)n);
        }
    }
}

unsigned wave_peak(const struct wave *w, unsigned long from, unsigned long to)
{
    unsigned long i;
    unsigned peak = 0;
    span(w, &from, &to);
    for (i = from * w->channels; i < to * w->channels; i++) {
        unsigned v = w->pcm[i] < 0 ? (unsigned)-(int)w->pcm[i] : (unsigned)w->pcm[i];
        if (v > peak)
            peak = v;
    }
    return peak;
}

int wave_normalise(struct wave *w, unsigned long from, unsigned long to)
{
    unsigned long i;
    unsigned peak;
    span(w, &from, &to);
    peak = wave_peak(w, from, to);
    if (!peak || peak >= 32767)
        return 1000;
    for (i = from * w->channels; i < to * w->channels; i++)
        w->pcm[i] = clip((long)w->pcm[i] * 32767 / (long)peak);
    return (int)(32767ul * 1000 / peak);
}

void wave_silence(struct wave *w, unsigned long from, unsigned long to)
{
    span(w, &from, &to);
    if (to > from)
        memset(w->pcm + from * w->channels, 0, (size_t)(to - from) * w->channels * sizeof *w->pcm);
}

void wave_reverse(struct wave *w, unsigned long from, unsigned long to)
{
    unsigned c;
    span(w, &from, &to);
    for (; to > from + 1; from++, to--)
        for (c = 0; c < w->channels; c++) {
            short t = w->pcm[from * w->channels + c];
            w->pcm[from * w->channels + c] = w->pcm[(to - 1) * w->channels + c];
            w->pcm[(to - 1) * w->channels + c] = t;
        }
}

int wave_set_channels(struct wave *w, unsigned channels)
{
    unsigned long i;
    if (channels < 1 || channels > 2)
        return WAVE_E_ARGUMENT;
    if (channels == w->channels)
        return WAVE_OK;
    if (channels == 2) {
        short *p;
        if (w->frames > MAX_SAMPLES / 2)
            return WAVE_E_MEMORY;
        if (!(p = realloc(w->pcm, (size_t)(w->frames ? w->frames : 1) * 2 * sizeof *p)))
            return WAVE_E_MEMORY;
        w->pcm = p, w->room = w->frames ? w->frames : 1;
        for (i = w->frames; i-- > 0; )
            p[i * 2] = p[i * 2 + 1] = p[i];
    } else {
        for (i = 0; i < w->frames; i++)
            w->pcm[i] = (short)(((long)w->pcm[i * 2] + w->pcm[i * 2 + 1]) / 2);
        w->room *= 2;                           /* (the same memory holds twice the frames now) */
    }
    w->channels = channels;
    return WAVE_OK;
}

int wave_resample(struct wave *w, unsigned rate)
{
    unsigned long long n64;
    unsigned long n, j;
    unsigned c, ch = w->channels;
    short *out;
    if (rate < 1000 || rate > 192000)
        return WAVE_E_ARGUMENT;
    if (rate == w->rate)
        return WAVE_OK;
    n64 = (unsigned long long)w->frames * rate / w->rate;
    if (n64 > MAX_SAMPLES / ch)
        return WAVE_E_MEMORY;
    n = (unsigned long)n64;
    if (!(out = malloc((size_t)(n ? n : 1) * ch * sizeof *out)))
        return WAVE_E_MEMORY;
    for (j = 0; j < n; j++) {
        unsigned long long p = (unsigned long long)j * w->rate;
        unsigned long i = (unsigned long)(p / rate);
        if (rate > w->rate) {                   /* up: on the line between the two samples it falls between */
            long long frac = (long long)(p % rate);
            unsigned long i2 = i + 1 < w->frames ? i + 1 : i;
            for (c = 0; c < ch; c++) {
                long a = w->pcm[i * ch + c], b = w->pcm[i2 * ch + c];
                out[j * ch + c] = (short)(a + (long)((long long)(b - a) * frac / (long long)rate));
            }
        } else {                                /* down: the mean of the samples that fall together */
            unsigned long e = (unsigned long)(((unsigned long long)(j + 1) * w->rate) / rate), k;
            if (e > w->frames)
                e = w->frames;
            if (e <= i)
                e = i + 1;
            for (c = 0; c < ch; c++) {
                long long sum = 0;
                for (k = i; k < e; k++)
                    sum += w->pcm[k * ch + c];
                out[j * ch + c] = (short)(sum / (long long)(e - i));
            }
        }
    }
    free(w->pcm);
    w->pcm = out, w->frames = n, w->room = n ? n : 1, w->rate = rate;
    return WAVE_OK;
}

void wave_range(const struct wave *w, unsigned long from, unsigned long to, unsigned channel, short *low, short *high)
{
    unsigned long i;
    short lo = 0, hi = 0;
    span(w, &from, &to);
    if (channel >= w->channels)
        channel = 0;
    for (i = from; i < to; i++) {
        short s = w->pcm[i * w->channels + channel];
        if (i == from || s < lo)
            lo = s;
        if (i == from || s > hi)
            hi = s;
    }
    *low = lo, *high = hi;
}

/* ---- IMA ADPCM: a sample as 4 bits, the difference from a prediction in steps of a size that follows the sound */

static const short ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const signed char ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

struct ima {
    long pred;
    int index;
};

static short ima_decode(struct ima *s, unsigned code)
{
    long step = ima_step[s->index], d = step >> 3;
    if (code & 4)
        d += step;
    if (code & 2)
        d += step >> 1;
    if (code & 1)
        d += step >> 2;
    s->pred = clip(code & 8 ? s->pred - d : s->pred + d);
    s->index += ima_index[code & 15];
    s->index = s->index < 0 ? 0 : s->index > 88 ? 88 : s->index;
    return (short)s->pred;
}

static unsigned ima_encode(struct ima *s, short sample)
{
    long step = ima_step[s->index], diff = sample - s->pred;
    unsigned code = 0;
    if (diff < 0)
        code = 8, diff = -diff;
    if (diff >= step)
        code |= 4, diff -= step;
    if (diff >= step >> 1)
        code |= 2, diff -= step >> 1;
    if (diff >= step >> 2)
        code |= 1;
    ima_decode(s, code);                        /* (the packer goes on from what the unpacker will have) */
    return code;
}

/* ---- WAV files */

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned long le32(const unsigned char *p)
{
    return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

#define PIECE 8192u

/* A WAV being read, piece by piece (the Player's; wave_load is all of its pieces). */
struct wave_in {
    FILE *f;
    unsigned tag, channels, align, per_block;
    unsigned long rate, data, frames;
    unsigned long at;                   /* the next frame to be given */
    unsigned char *raw;                 /* PIECE bytes, or an ADPCM block's */
    short *block;                       /* ADPCM: a block unpacked ... */
    unsigned long block_at;             /* ... which one (~0: none) ... */
    unsigned block_n;                   /* ... and how many frames it gave */
};

struct wave_in *wave_in_open(const char *path, unsigned *rate_out, unsigned *channels_out, unsigned long *frames_out,
                             char *detail, unsigned room, int *err)
{
    struct wave_in *in = calloc(1, sizeof *in);
    FILE *f = NULL;
    unsigned char h[40];
    unsigned tag = 0, channels = 0, bits = 0, align = 0, per_block = 0;
    unsigned long rate = 0, data = 0, fact = 0, at, size, frames;
    long end;
    int r = WAVE_E_MEMORY, have_fmt = 0, dummy;
    if (!err)
        err = &dummy;
    if (!in)
        goto out;
    r = WAVE_E_OPEN;
    if (!(f = file_open(path, "rb")) || fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < 0)
        goto out;
    rewind(f);
    r = WAVE_E_KIND;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        goto out;
    r = WAVE_E_DAMAGED;
    for (at = 12; ; at += 8 + size + (size & 1)) {      /* the chunks, to the samples' */
        if (at + 8 > (unsigned long)end || fseek(f, (long)at, SEEK_SET) != 0 || fread(h, 1, 8, f) != 8)
            goto out;
        size = le32(h + 4);
        if (!memcmp(h, "fmt ", 4)) {
            unsigned n = size < 40 ? (unsigned)size : 40;
            if (n < 16 || fread(h, 1, n, f) != n)
                goto out;
            tag = le16(h), channels = le16(h + 2), rate = le32(h + 4), align = le16(h + 12), bits = le16(h + 14);
            if (tag == 0xFFFE && n >= 26)       /* "extensible": the kind is further on */
                tag = le16(h + 24);
            if (tag == 0x11 && n >= 20)
                per_block = le16(h + 18);
            have_fmt = 1;
        } else if (!memcmp(h, "fact", 4)) {
            if (size >= 4 && fread(h, 1, 4, f) == 4)
                fact = le32(h);
        } else if (!memcmp(h, "data", 4)) {
            data = at + 8;
            if (size > (unsigned long)end - data)
                size = (unsigned long)end - data;       /* (it says more than there is) */
            break;
        }
        if (size > 0x7FFFFFF0ul)
            goto out;
    }
    if (!have_fmt)
        goto out;
    r = WAVE_E_FORMAT;
    if (channels < 1 || channels > 2 || rate < 1000 || rate > 192000)
        goto out;
    if (tag == 1) {
        unsigned bytes = (bits + 7) / 8;
        if (bytes < 1 || bytes > 4)
            goto out;
        align = bytes * channels;
        frames = size / align;
        if (detail)
            snprintf(detail, room, "%u bits", bytes * 8);
    } else if (tag == 0x11) {
        if (bits != 4 || align < 4 * channels + channels || align > 8192 || align % (4 * channels))
            goto out;
        if (!per_block || per_block > (align / channels - 4) * 2 + 1)
            per_block = (align / channels - 4) * 2 + 1;
        frames = size / align * per_block;
        if (fact && fact < frames)              /* (the last block is filled out: "fact" has the true count) */
            frames = fact;
        if (detail)
            snprintf(detail, room, "ADPCM");
    } else {
        goto out;
    }
    r = WAVE_E_MEMORY;
    if (!(in->raw = malloc(tag == 0x11 && align > PIECE ? align : PIECE)))
        goto out;
    if (tag == 0x11 && !(in->block = malloc((size_t)per_block * channels * sizeof *in->block)))
        goto out;
    r = WAVE_E_DAMAGED;
    if (fseek(f, (long)data, SEEK_SET) != 0)
        goto out;
    in->f = f;
    in->tag = tag, in->channels = channels, in->align = align, in->per_block = per_block;
    in->rate = rate, in->data = data, in->frames = frames;
    in->block_at = ~0ul;
    if (rate_out)
        *rate_out = (unsigned)rate;
    if (channels_out)
        *channels_out = channels;
    if (frames_out)
        *frames_out = frames;
    *err = WAVE_OK;
    return in;
out:
    if (f)
        fclose(f);
    if (in) {
        free(in->raw);
        free(in->block);
    }
    free(in);
    *err = r;
    return NULL;
}

void wave_in_close(struct wave_in *in)
{
    if (!in)
        return;
    fclose(in->f);
    free(in->raw);
    free(in->block);
    free(in);
}

/* ADPCM: the block that has frame `at`, unpacked. 0: the file has no such block. */
static int in_block(struct wave_in *in, unsigned long b)
{
    struct ima s[2];
    unsigned c, k, n = 1, channels = in->channels, bytes = in->align / channels - 4;   /* (a channel's packed bytes) */
    const unsigned char *buf = in->raw;
    if (in->block_at == b)
        return 1;
    in->block_at = ~0ul;
    if (fseek(in->f, (long)(in->data + b * in->align), SEEK_SET) != 0 || fread(in->raw, 1, in->align, in->f) != in->align)
        return 0;
    for (c = 0; c < channels; c++) {
        s[c].pred = (short)le16(buf + c * 4);
        s[c].index = buf[c * 4 + 2] > 88 ? 88 : buf[c * 4 + 2];
        in->block[c] = (short)s[c].pred;
    }
    /* Then four bytes of a channel's codes (eight samples, the low half of a byte first), four of the next
       channel's, and so on. */
    for (k = 0; k < bytes * 2 && n < in->per_block; k++, n++)
        for (c = 0; c < channels; c++) {
            unsigned byte = 4 * channels + (k / 8) * 4 * channels + c * 4 + (k % 8) / 2;
            in->block[n * channels + c] = ima_decode(&s[c], k & 1 ? buf[byte] >> 4 : buf[byte] & 15);
        }
    in->block_at = b, in->block_n = n;
    return 1;
}

unsigned long wave_in_read(struct wave_in *in, short *pcm, unsigned long frames)
{
    unsigned long done = 0;
    unsigned channels = in->channels;
    if (frames > in->frames - in->at)
        frames = in->frames - in->at;
    if (in->tag == 1) {
        unsigned bytes = in->align / channels, per = PIECE / in->align, i, n;
        while (done < frames) {
            unsigned want = frames - done < per ? (unsigned)(frames - done) : per;
            if ((n = (unsigned)fread(in->raw, in->align, want, in->f)) == 0)
                break;
            for (i = 0; i < n * channels; i++) {
                const unsigned char *p = in->raw + i * bytes;
                pcm[done * channels + i] = bytes == 1 ? (short)((p[0] - 128) * 256)
                                                      : (short)(p[bytes - 2] | (unsigned)p[bytes - 1] << 8);
            }
            done += n;
        }
    } else {
        while (done < frames) {
            unsigned long b = (in->at + done) / in->per_block;
            unsigned off = (unsigned)((in->at + done) % in->per_block), n;
            if (!in_block(in, b) || off >= in->block_n)
                break;
            n = in->block_n - off;
            if (n > frames - done)
                n = (unsigned)(frames - done);
            memcpy(pcm + done * channels, in->block + off * channels, (size_t)n * channels * sizeof *pcm);
            done += n;
        }
    }
    in->at += done;
    return done;
}

int wave_in_seek(struct wave_in *in, unsigned long frame)
{
    if (frame > in->frames)
        frame = in->frames;
    if (in->tag == 1 && fseek(in->f, (long)(in->data + frame * in->align), SEEK_SET) != 0)
        return WAVE_E_OPEN;
    in->at = frame;
    return WAVE_OK;
}

int wave_load(const char *path, struct wave *w)
{
    struct wave_in *in;
    unsigned rate, channels;
    unsigned long frames;
    int r;
    memset(w, 0, sizeof *w);
    if (!(in = wave_in_open(path, &rate, &channels, &frames, NULL, 0, &r)))
        return r;
    if ((r = wave_init(w, rate, channels)) == WAVE_OK && frames && (r = room_for(w, frames)) == WAVE_OK)
        w->frames = wave_in_read(in, w->pcm, frames);   /* (a file cut short: what it has) */
    wave_in_close(in);
    if (r != WAVE_OK)
        wave_free(w);
    return r;
}

static void header(unsigned char *h, unsigned tag, const struct wave *w, unsigned align, unsigned bits, unsigned long bytes,
                   unsigned per_block, unsigned *size)
{
    unsigned fmt = tag == 0x11 ? 20 : 16, at;
    memcpy(h, "RIFF", 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, fmt);
    put16(h + 20, tag), put16(h + 22, w->channels);
    put32(h + 24, w->rate);
    put32(h + 28, tag == 0x11 ? (unsigned long)w->rate * align / per_block : (unsigned long)w->rate * align);
    put16(h + 32, align), put16(h + 34, bits);
    at = 36;
    if (tag == 0x11) {
        put16(h + 36, 2), put16(h + 38, per_block);
        memcpy(h + 40, "fact", 4);
        put32(h + 44, 4), put32(h + 48, w->frames);
        at = 52;
    }
    memcpy(h + at, "data", 4);
    put32(h + at + 4, bytes);
    *size = at + 8;
    put32(h + 4, *size - 8 + bytes + (bytes & 1));
}

int wave_save(const char *path, const struct wave *w, int format)
{
    FILE *f;
    unsigned char h[64], *buf;
    unsigned hsize, ch = w->channels, i;
    unsigned long done = 0, bytes;
    int ok = 1;
    if (format != WAVE_PCM16 && format != WAVE_PCM8 && format != WAVE_ADPCM)
        return WAVE_E_ARGUMENT;
    if (!(buf = malloc(PIECE)))
        return WAVE_E_MEMORY;
    if (!(f = file_open(path, "wb"))) {
        free(buf);
        return WAVE_E_OPEN;
    }
    if (format == WAVE_ADPCM) {
        unsigned k = w->rate / 11025 ? w->rate / 11025 : 1, align, per;
        struct ima s[2];
        if (k > 4)
            k = 4;
        align = 256 * ch * k, per = (align / ch - 4) * 2 + 1;
        bytes = (w->frames + per - 1) / per * align;
        header(h, 0x11, w, align, 4, bytes, per, &hsize);
        ok = fwrite(h, 1, hsize, f) == hsize;
        memset(s, 0, sizeof s);
        while (ok && done < w->frames) {
            unsigned c, n;
            memset(buf, 0, align);
            for (c = 0; c < ch; c++) {          /* the block's first sample whole; the step goes on from the last */
                s[c].pred = w->pcm[done * ch + c];
                put16(buf + c * 4, (unsigned)s[c].pred & 0xFFFF);
                buf[c * 4 + 2] = (unsigned char)s[c].index;
            }
            for (n = 1; n < per; n++)
                for (c = 0; c < ch; c++) {
                    unsigned long at = done + n < w->frames ? done + n : w->frames - 1;        /* (the end: held) */
                    unsigned kk = n - 1, byte = 4 * ch + (kk / 8) * 4 * ch + c * 4 + (kk % 8) / 2;
                    unsigned code = ima_encode(&s[c], w->pcm[at * ch + c]);
                    buf[byte] |= (unsigned char)(kk & 1 ? code << 4 : code);
                }
            ok = fwrite(buf, 1, align, f) == align;
            done += per;
        }
    } else {
        unsigned bps = format == WAVE_PCM8 ? 1 : 2, per = PIECE / (bps * ch);
        bytes = w->frames * ch * bps;
        header(h, 1, w, bps * ch, bps * 8, bytes, 0, &hsize);
        ok = fwrite(h, 1, hsize, f) == hsize;
        while (ok && done < w->frames) {
            unsigned n = w->frames - done < per ? (unsigned)(w->frames - done) : per;
            for (i = 0; i < n * ch; i++) {
                short v = w->pcm[done * ch + i];
                if (bps == 1)
                    buf[i] = (unsigned char)((v >> 8) + 128);
                else
                    put16(buf + i * 2, (unsigned)v & 0xFFFF);
            }
            ok = fwrite(buf, bps * ch, n, f) == n;
            done += n;
        }
        if (ok && (bytes & 1))                  /* (a chunk of an odd size is filled to an even one) */
            ok = fputc(0, f) != EOF;
    }
    free(buf);
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        file_unlink(path);                      /* (half a file is none) */
        return WAVE_E_OPEN;
    }
    return WAVE_OK;
}

/* ---- a WAV written as its samples come */

struct wave_out {
    FILE *f;
    char *path;
    struct wave w;                      /* (its rate and channels; frames: those written) */
    int bad;
};

struct wave_out *wave_out_begin(const char *path, unsigned rate, unsigned channels, int *err)
{
    struct wave_out *o = calloc(1, sizeof *o);
    unsigned char h[64];
    unsigned hsize;
    int r = WAVE_E_MEMORY, dummy;
    if (!err)
        err = &dummy;
    if (o && (r = wave_init(&o->w, rate, channels)) == WAVE_OK) {
        r = WAVE_E_MEMORY;
        if ((o->path = malloc(strlen(path) + 1)) != NULL) {
            strcpy(o->path, path);
            r = WAVE_E_OPEN;
            if ((o->f = file_open(path, "wb")) != NULL) {
                header(h, 1, &o->w, 2 * channels, 16, 0, 0, &hsize);
                if (fwrite(h, 1, hsize, o->f) == hsize) {
                    *err = WAVE_OK;
                    return o;
                }
                fclose(o->f);
                file_unlink(path);
            }
        }
    }
    if (o)
        free(o->path);
    free(o);
    *err = r;
    return NULL;
}

int wave_out_write(struct wave_out *o, const short *pcm, unsigned long frames)
{
    unsigned char buf[1024];
    unsigned long done = 0, total = frames * o->w.channels;
    if (o->bad)
        return WAVE_E_OPEN;
    if (o->w.frames + frames > 0x7FFFFF00ul / (2 * o->w.channels)) {   /* (a WAV's sizes are 32 bits) */
        o->bad = 1;
        return WAVE_E_OPEN;
    }
    while (done < total) {
        unsigned n = total - done < sizeof buf / 2 ? (unsigned)(total - done) : (unsigned)(sizeof buf / 2), i;
        for (i = 0; i < n; i++)
            put16(buf + i * 2, (unsigned)pcm[done + i] & 0xFFFF);
        if (fwrite(buf, 2, n, o->f) != n) {
            o->bad = 1;
            return WAVE_E_OPEN;
        }
        done += n;
    }
    o->w.frames += frames;
    return WAVE_OK;
}

int wave_out_end(struct wave_out *o)
{
    unsigned char h[64];
    unsigned hsize;
    int ok = !o->bad;
    header(h, 1, &o->w, 2 * o->w.channels, 16, o->w.frames * o->w.channels * 2, 0, &hsize);
    if (ok)                                     /* the sizes, now that they are known */
        ok = fseek(o->f, 0, SEEK_SET) == 0 && fwrite(h, 1, hsize, o->f) == hsize;
    if (fclose(o->f) != 0)
        ok = 0;
    if (!ok)
        file_unlink(o->path);
    free(o->path);
    free(o);
    return ok ? WAVE_OK : WAVE_E_OPEN;
}

const char *wave_error(int err)
{
    switch (err) {
    case WAVE_OK: return "no error";
    case WAVE_E_OPEN: return "the file couldn't be read or written";
    case WAVE_E_KIND: return "not a WAV file";
    case WAVE_E_FORMAT: return "a WAV of a kind that isn't played here";
    case WAVE_E_DAMAGED: return "the file is damaged";
    case WAVE_E_MEMORY: return "not enough memory";
    default: return "it can't be done";
    }
}
