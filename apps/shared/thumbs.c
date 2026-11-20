/* Thumbnails, for Files' thumbnail view and Images' filmstrip (phase E; docs/v1/apps/files.md, Views; thumbs.h):
 * a picture's file read and decoded (picfile.c: the viewer's and the editor's own decoders), made small enough for
 * the toolkit's list's square (GUI_THUMB each way at most, its shape kept), and kept in a folder beside the
 * programs (THUMBS, in the folder thumb_home names: GLOS's own apps are all in one, so they share it) under a
 * name made from its path, so that the next time it is one small file read: a big JPEG takes a 486 many seconds.
 *
 * A kept thumbnail is its picture's only while the file's size and date are what they were when it was made (its
 * head says them, and the path's second hash, in case two paths make one name). The folder is kept to
 * THUMBS_FILES files and THUMBS_BYTES bytes: when it has more, the oldest go (thumb_trim, once a run of Files).
 *
 * thumb_make runs on a worker's thread (the toolkit's tasks): it touches no widget, and its files are opened by
 * the toolkit's calls and read by the C library's locked ones. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "picfile.h"
#include "thumbs.h"

#define THUMBS_FILES 400
#define THUMBS_BYTES (6ul << 20)
#define HEAD 20
#define MAX_PIXELS 8000000l             /* (a picture of more is not decoded for its thumbnail: the decoders
                                           want eight bytes a pixel while they work, 64 MB for this many) */

static unsigned long hash(const char *path, unsigned long h, unsigned long mul)
{
    for (; *path; path++) {
        unsigned char c = (unsigned char)*path;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c == '/')
            c = '\\';
        h = ((h ^ c) * mul) & 0xFFFFFFFFul;
    }
    return h;
}

static char home[260];                  /* thumb_home: THUMBS is in it ("": nothing is kept) */

void thumb_home(const char *dir) { snprintf(home, sizeof home, "%s", dir ? dir : ""); }

static void cache_dir(char *out, size_t room) { snprintf(out, room, "%s\\THUMBS", home); }

static void cache_name(char *out, size_t room, const char *path)
{
    snprintf(out, room, "%s\\THUMBS\\%08lX.THM", home, hash(path, 2166136261ul, 16777619ul));
}

static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, unsigned long v) { put16(p, (unsigned)(v & 0xFFFF)), put16(p + 2, (unsigned)(v >> 16)); }
static unsigned get16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }

static void head(unsigned char *h, const char *path, const struct gui_dirent *e, int w, int ht)
{
    memcpy(h, "GTH1", 4);
    put16(h + 4, (unsigned)w), put16(h + 6, (unsigned)ht);
    put32(h + 8, e->size);
    put16(h + 12, e->date), put16(h + 14, e->time);
    put32(h + 16, hash(path, 5381ul, 33ul));
}

static unsigned *cache_read(const char *path, const struct gui_dirent *e, int *pw, int *ph)
{
    char name[300];
    unsigned char h[HEAD], want[HEAD], *rgb;
    unsigned *px = NULL;
    FILE *f;
    int w, ht, i;
    cache_name(name, sizeof name, path);
    if (!home[0] || !(f = gui_fopen(name, "rb")))
        return NULL;
    if (fread(h, 1, HEAD, f) == HEAD) {
        w = (int)get16(h + 4), ht = (int)get16(h + 6);
        head(want, path, e, w, ht);
        if (!memcmp(h, want, HEAD) && w >= 1 && ht >= 1 && w <= GUI_THUMB && ht <= GUI_THUMB
            && (rgb = malloc((size_t)w * (size_t)ht * 3)) != NULL) {
            if (fread(rgb, 3, (size_t)w * (size_t)ht, f) == (size_t)w * (size_t)ht
                && (px = malloc((size_t)w * (size_t)ht * sizeof *px)) != NULL) {
                for (i = 0; i < w * ht; i++)
                    px[i] = (unsigned)rgb[i * 3] << 16 | (unsigned)rgb[i * 3 + 1] << 8 | rgb[i * 3 + 2];
                *pw = w, *ph = ht;
            }
            free(rgb);
        }
    }
    fclose(f);
    return px;
}

static void cache_write(const char *path, const struct gui_dirent *e, const unsigned *px, int w, int ht)
{
    char name[300], dir[300];
    unsigned char h[HEAD], *rgb;
    FILE *f;
    int i, ok;
    if (!home[0] || !(rgb = malloc((size_t)w * (size_t)ht * 3)))
        return;
    for (i = 0; i < w * ht; i++)
        rgb[i * 3] = (unsigned char)(px[i] >> 16), rgb[i * 3 + 1] = (unsigned char)(px[i] >> 8), rgb[i * 3 + 2] = (unsigned char)px[i];
    cache_dir(dir, sizeof dir);
    cache_name(name, sizeof name, path);
    if (!(f = gui_fopen(name, "wb"))) {         /* (the first there has been: its folder made) */
        gui_mkdir(dir);
        f = gui_fopen(name, "wb");
    }
    if (f) {
        head(h, path, e, w, ht);
        ok = fwrite(h, 1, HEAD, f) == HEAD && fwrite(rgb, 3, (size_t)w * (size_t)ht, f) == (size_t)w * (size_t)ht;
        if (fclose(f) != 0 || !ok)
            gui_unlink(name);                   /* (a full disc, a disc that is not written: no half of one is left) */
    }
    free(rgb);
}

/* The picture made small: each of the thumbnail's pixels the mean of those it stands for. */
static unsigned *shrink(const unsigned *px, int w, int h, int *pw, int *ph)
{
    int tw = w, th = h, x, y;
    unsigned *out;
    if (w > GUI_THUMB || h > GUI_THUMB) {
        if (w >= h)
            tw = GUI_THUMB, th = (int)((long)h * GUI_THUMB / w);
        else
            th = GUI_THUMB, tw = (int)((long)w * GUI_THUMB / h);
    }
    if (tw < 1)
        tw = 1;
    if (th < 1)
        th = 1;
    if (!(out = malloc((size_t)tw * (size_t)th * sizeof *out)))
        return NULL;
    for (y = 0; y < th; y++) {
        int y0 = (int)((long)y * h / th), y1 = (int)((long)(y + 1) * h / th);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (x = 0; x < tw; x++) {
            int x0 = (int)((long)x * w / tw), x1 = (int)((long)(x + 1) * w / tw), sx, sy;
            unsigned long r = 0, g = 0, b = 0, n;
            if (x1 <= x0)
                x1 = x0 + 1;
            for (sy = y0; sy < y1; sy++) {
                const unsigned *row = px + (size_t)sy * (size_t)w;
                for (sx = x0; sx < x1; sx++)
                    r += row[sx] >> 16 & 255, g += row[sx] >> 8 & 255, b += row[sx] & 255;
            }
            n = (unsigned long)(x1 - x0) * (unsigned long)(y1 - y0);
            out[y * tw + x] = (unsigned)((r / n) << 16 | (g / n) << 8 | b / n);
        }
    }
    *pw = tw, *ph = th;
    return out;
}

unsigned *thumb_make(const char *path, const struct gui_dirent *e, int *pw, int *ph, int *made)
{
    unsigned char *d;
    unsigned *px, *out;
    size_t n;
    char kind[8];
    int w = 0, h = 0;
    if (made)
        *made = 0;
    if ((out = cache_read(path, e, pw, ph)) != NULL)
        return out;
    if (!(d = pic_read_all(path, &n)))
        return NULL;
    if (pic_info(d, n, &w, &h) && (w < 1 || h < 1 || (long)w * h > MAX_PIXELS)) {
        free(d);
        return NULL;
    }
    px = pic_decode(d, n, path, &w, &h, kind, sizeof kind, 0xFFFFFF);
    free(d);
    if (!px)
        return NULL;
    out = w >= 1 && h >= 1 && (long)w * h <= MAX_PIXELS ? shrink(px, w, h, pw, ph) : NULL;
    free(px);
    if (out) {
        cache_write(path, e, out, *pw, *ph);
        if (made)
            *made = 1;
    }
    return out;
}

static int older(const void *a, const void *b)
{
    const struct gui_dirent *x = a, *y = b;
    if (x->date != y->date)
        return x->date < y->date ? -1 : 1;
    return x->time < y->time ? -1 : x->time > y->time;
}

void thumb_trim(void)
{
    char dir[300], p[300];
    struct gui_dirent *e = NULL;
    unsigned long bytes = 0;
    int n, i, files = 0;
    if (!home[0])
        return;
    cache_dir(dir, sizeof dir);
    n = gui_dir_read(dir, &e);
    for (i = 0; i < n; i++)
        if (!e[i].dir)
            files++, bytes += e[i].size;
    if (files > THUMBS_FILES || bytes > THUMBS_BYTES) {
        qsort(e, (size_t)n, sizeof *e, older);
        for (i = 0; i < n && (files > THUMBS_FILES * 3 / 4 || bytes > THUMBS_BYTES / 4 * 3); i++) {
            if (e[i].dir)
                continue;
            gui_path_join(p, sizeof p, dir, e[i].name);
            if (gui_unlink(p) == 0)
                files--, bytes -= e[i].size;
        }
    }
    free(e);
}

int thumb_is_picture(const char *name) { return pic_is_picture(name); }
