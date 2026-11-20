/* A sound file as samples, had as they are wanted (source.h): the Player's files. A WAV is read piece by piece
 * from its file (wave.c's wave_in); a module is read whole (they are small) and played by mod.c, s3m.c or xm.c;
 * an MP3 or an Ogg Vorbis file is unpacked piece by piece (packed.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mod.h"
#include "packed.h"
#include "s3m.h"
#include "source.h"
#include "wave.h"
#include "xm.h"

#define MOD_MAX (16ul << 20)            /* a module's file at most */

static void *open_plain(const char *path, const char *mode) { return fopen(path, mode); }
static void *(*file_open)(const char *path, const char *mode) = open_plain;

void source_files(void *(*open)(const char *path, const char *mode))
{
    file_open = open ? open : open_plain;
}

unsigned char *source_whole(const char *path, unsigned long max, unsigned long *size)
{
    FILE *f = file_open(path, "rb");
    unsigned char *bytes = NULL;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && (unsigned long)n <= max && fseek(f, 0, SEEK_SET) == 0
        && (bytes = malloc((size_t)n)) != NULL) {
        if (fread(bytes, 1, (size_t)n, f) != (size_t)n) {
            free(bytes);
            bytes = NULL;
        }
        *size = (unsigned long)n;
    }
    fclose(f);
    return bytes;
}

struct source {
    struct source_info info;
    struct wave_in *wav;
    struct mod *mod;                    /* a module's player: one of the three */
    struct s3m *s3m;
    struct xm *xm;
    struct packed *packed;              /* an MP3's or an Ogg Vorbis file's unpacker */
    unsigned char *bytes;               /* the module's file */
    unsigned long at;                   /* frames given so far */
};

static int of_wave(int e)
{
    switch (e) {
    case WAVE_OK: return SOURCE_OK;
    case WAVE_E_OPEN: return SOURCE_E_OPEN;
    case WAVE_E_KIND: return SOURCE_E_KIND;
    case WAVE_E_FORMAT: return SOURCE_E_FORMAT;
    case WAVE_E_MEMORY: return SOURCE_E_MEMORY;
    default: return SOURCE_E_DAMAGED;
    }
}

struct source *source_open(const char *path, unsigned rate, int *err)
{
    struct source *s = calloc(1, sizeof *s);
    unsigned char head[12];
    FILE *f;
    long size;
    int dummy, e;
    if (!err)
        err = &dummy;
    *err = SOURCE_E_MEMORY;
    if (!s)
        return NULL;
    *err = SOURCE_E_OPEN;
    if (!(f = file_open(path, "rb")))
        goto bad;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        goto bad;
    }
    *err = SOURCE_E_KIND;
    if (fread(head, 1, 12, f) != 12) {
        fclose(f);
        goto bad;
    }
    if (!memcmp(head, "RIFF", 4) && !memcmp(head + 8, "WAVE", 4)) {
        char detail[40];
        fclose(f);
        s->wav = wave_in_open(path, &s->info.rate, &s->info.channels, &s->info.frames, detail, sizeof detail, &e);
        if (!s->wav) {
            *err = of_wave(e);
            goto bad;
        }
        strcpy(s->info.kind, "WAV");
        snprintf(s->info.detail, sizeof s->info.detail, "%s", detail);
    } else if (!memcmp(head, "OggS", 4) || !memcmp(head, "ID3", 3) || (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0)) {
        /* Ogg, or what an MP3 begins with (its tag, or a frame): unpacked as it is played */
        s->packed = packed_open(f, head, 12, (unsigned long)size, &s->info, &e);
        if (!s->packed) {
            fclose(f);
            *err = e;
            goto bad;
        }
    } else {                                    /* a module, if it is anything: read whole */
        const char *title;
        unsigned channels;
        if (size < 60 || (unsigned long)size > MOD_MAX) {
            fclose(f);
            goto bad;
        }
        *err = SOURCE_E_MEMORY;
        if (!(s->bytes = malloc((size_t)size))) {
            fclose(f);
            goto bad;
        }
        rewind(f);
        *err = SOURCE_E_OPEN;
        if (fread(s->bytes, 1, (size_t)size, f) != (size_t)size) {
            fclose(f);
            goto bad;
        }
        /* (The three players' errors are the same numbers: MOD_E_.) */
        if (!memcmp(s->bytes, "Extended Module: ", 17)) {
            s->xm = xm_open(s->bytes, (unsigned long)size, rate, &e);
            if (!s->xm && e == XM_E_KIND) {     /* (an XM of before FastTracker 2's last form) */
                fclose(f);
                *err = SOURCE_E_FORMAT;
                goto bad;
            }
        } else if (size >= 0x60 && !memcmp(s->bytes + 0x2C, "SCRM", 4)) {
            s->s3m = s3m_open(s->bytes, (unsigned long)size, rate, &e);
        } else {
            s->mod = mod_open(s->bytes, (unsigned long)size, rate, &e);
        }
        if (!s->mod && !s->s3m && !s->xm && e == MOD_E_KIND) {
            /* no module: an MP3 that begins with neither tag nor frame? */
            free(s->bytes);
            s->bytes = NULL;
            s->packed = packed_open(f, head, 12, (unsigned long)size, &s->info, &e);
            if (!s->packed) {
                fclose(f);
                *err = e;
                goto bad;
            }
            *err = SOURCE_OK;
            return s;
        }
        fclose(f);
        if (!s->mod && !s->s3m && !s->xm) {
            *err = e == MOD_E_MEMORY ? SOURCE_E_MEMORY : SOURCE_E_DAMAGED;
            goto bad;
        }
        s->info.rate = rate, s->info.channels = 2;
        if (s->xm) {
            strcpy(s->info.kind, "XM");
            s->info.frames = xm_length(s->xm), title = xm_title(s->xm), channels = xm_channels(s->xm);
        } else if (s->s3m) {
            strcpy(s->info.kind, "S3M");
            s->info.frames = s3m_length(s->s3m), title = s3m_title(s->s3m), channels = s3m_channels(s->s3m);
        } else {
            strcpy(s->info.kind, "MOD");
            s->info.frames = mod_length(s->mod), title = mod_title(s->mod), channels = mod_channels(s->mod);
        }
        snprintf(s->info.title, sizeof s->info.title, "%s", title);
        snprintf(s->info.detail, sizeof s->info.detail, "%u channels", channels);
    }
    *err = SOURCE_OK;
    return s;
bad:
    source_close(s);
    return NULL;
}

void source_close(struct source *s)
{
    if (!s)
        return;
    if (s->wav)
        wave_in_close(s->wav);
    if (s->mod)
        mod_free(s->mod);
    if (s->s3m)
        s3m_free(s->s3m);
    xm_free(s->xm);
    packed_close(s->packed);
    free(s->bytes);
    free(s);
}

const struct source_info *source_info(const struct source *s) { return &s->info; }
unsigned long source_tell(const struct source *s) { return s->at; }

unsigned long source_read(struct source *s, short *pcm, unsigned long frames)
{
    unsigned long n = s->wav ? wave_in_read(s->wav, pcm, frames) : s->packed ? packed_read(s->packed, pcm, frames)
                      : s->xm ? xm_render(s->xm, pcm, frames) : s->s3m ? s3m_render(s->s3m, pcm, frames)
                      : mod_render(s->mod, pcm, frames);
    s->at += n;
    return n;
}

int source_seek(struct source *s, unsigned long frame)
{
    if (frame > s->info.frames)
        frame = s->info.frames;
    if (s->wav) {
        if (wave_in_seek(s->wav, frame) != WAVE_OK)
            return -1;
    } else if (s->packed) {
        if (packed_seek(s->packed, frame) != 0)
            return -1;
    } else if (s->xm) {
        xm_seek(s->xm, frame);
    } else if (s->s3m) {
        s3m_seek(s->s3m, frame);
    } else {
        mod_seek(s->mod, frame);
    }
    s->at = frame;
    return 0;
}

const char *source_error(int err)
{
    switch (err) {
    case SOURCE_OK: return "no error";
    case SOURCE_E_OPEN: return "the file couldn't be read";
    case SOURCE_E_KIND: return "not a sound file of a kind that is played here";
    case SOURCE_E_FORMAT: return "packed in a way that isn't played here";
    case SOURCE_E_MEMORY: return "not enough memory";
    default: return "the file is damaged";
    }
}
