/* The clipboard's formats (toolkit 2, step C7; docs/v1/toolkit-2.md 9, on API 2.0's CLIP): text, pictures and
 * lists of files, alone or several at once (a transaction: the clipboard changes when it is done, so a paste never
 * sees half of it). gui_clip_set and gui_clip_get (dialog.c) stay the plain text's. */
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

static int image_put(glos_handle t, const unsigned *argb, int w, int h)
{
    struct glos_clip_image head;
    size_t n = (size_t)w * (size_t)h * 4;
    unsigned char *d = malloc(sizeof head + n);
    int r;
    if (!d || w <= 0 || h <= 0) {
        free(d);
        return -1;
    }
    head.w = (unsigned)w, head.h = (unsigned)h;
    memcpy(d, &head, sizeof head);
    memcpy(d + sizeof head, argb, n);
    r = t ? glos_clip_add(t, GLOS_CLIP_IMAGE, d, (unsigned)(sizeof head + n))
          : glos_clip_put(GLOS_CLIP_IMAGE, d, (unsigned)(sizeof head + n));
    free(d);
    return r == 0 ? 0 : -1;
}

static int files_put(glos_handle t, const char *const *paths, int count)
{
    size_t n = 0, k;
    char *d, *p;
    int i, r;
    for (i = 0; i < count; i++)
        n += strlen(paths[i]) + 1;
    if (!(p = d = malloc(n ? n : 1)))
        return -1;
    for (i = 0; i < count; i++) {               /* each one's full path and a NUL */
        k = strlen(paths[i]) + 1;
        memcpy(p, paths[i], k);
        p += k;
    }
    r = t ? glos_clip_add(t, GLOS_CLIP_FILES, d, (unsigned)n) : glos_clip_put(GLOS_CLIP_FILES, d, (unsigned)n);
    free(d);
    return r == 0 ? 0 : -1;
}

int gui_clip_set_image(const unsigned *argb, int w, int h) { return image_put(0, argb, w, h); }
int gui_clip_set_files(const char *const *paths, int n) { return files_put(0, paths, n); }

glos_handle gui_clip_start(void) { return glos_clip_open(1); }

int gui_clip_put_text(glos_handle t, const char *text, size_t n)
{
    return glos_clip_add(t, GLOS_CLIP_TEXT, text, (unsigned)n) == 0 ? 0 : -1;
}

int gui_clip_put_image(glos_handle t, const unsigned *argb, int w, int h) { return image_put(t, argb, w, h); }
int gui_clip_put_files(glos_handle t, const char *const *paths, int n) { return files_put(t, paths, n); }

int gui_clip_put(glos_handle t, unsigned format, const void *data, size_t n)
{
    return glos_clip_add(t, format, data, (unsigned)n) == 0 ? 0 : -1;
}

int gui_clip_done(glos_handle t) { return glos_close(t) == 0 ? 0 : -1; }

int gui_clip_has(unsigned format)
{
    struct glos_clip_formats f;
    unsigned i;
    memset(&f, 0, sizeof f);
    f.size = sizeof f;
    if (glos_clip_formats(0, &f) != 0)
        return 0;
    for (i = 0; i < f.count && i < GLOS_CLIP_ITEMS; i++)
        if (f.format[i] == format)
            return 1;
    return 0;
}

/* A format's whole data (malloc'd, a NUL after it), or NULL. */
void *gui_clip_read(unsigned format, size_t *n)
{
    unsigned len = 0, got = 0;
    unsigned char *d;
    int r = glos_clip_get(format, NULL, 0, &len);
    if ((r != 0 && r != GLOS_E_ROOM) || !(d = malloc((size_t)len + 1)))
        return NULL;
    r = glos_clip_get(format, d, len, &got);
    if (r != 0 && r != GLOS_E_ROOM) {
        free(d);
        return NULL;
    }
    d[len] = 0;
    if (n)
        *n = len;
    return d;
}

unsigned *gui_clip_get_image(int *w, int *h)
{
    size_t n;
    struct glos_clip_image head;
    unsigned char *d = gui_clip_read(GLOS_CLIP_IMAGE, &n);
    unsigned *pix;
    if (!d)
        return NULL;
    memcpy(&head, d, sizeof head < n ? sizeof head : n);
    if (n < sizeof head || (size_t)head.w * head.h * 4 + sizeof head > n || !(pix = malloc((size_t)head.w * head.h * 4))) {
        free(d);
        return NULL;
    }
    memcpy(pix, d + sizeof head, (size_t)head.w * head.h * 4);
    *w = (int)head.w, *h = (int)head.h;
    free(d);
    return pix;
}

/* The files' paths: an array of them and the strings in one block (free it once), or NULL. */
char **gui_clip_get_files(int *count)
{
    size_t n, i, k = 0;
    char *d = gui_clip_read(GLOS_CLIP_FILES, &n), **out, *p;
    if (!d)
        return NULL;
    for (i = 0; i < n; i++)
        k += d[i] == 0;
    if (!(out = malloc((k + 1) * sizeof *out + n + 1))) {
        free(d);
        return NULL;
    }
    p = (char *)(out + k + 1);
    memcpy(p, d, n + 1);
    free(d);
    for (i = 0, k = 0; i < n; i += strlen(p + i) + 1)
        out[k++] = p + i;
    out[k] = NULL;
    *count = (int)k;
    return out;
}
