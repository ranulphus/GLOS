/* The clipboard's store (API 2.0's CLIP; docs/v1/phase-b.md step 6): what is on the clipboard is a content, data
 * in up to CLIP_ITEMS formats, counted by who refers to it: the store while it is the clipboard, and every
 * transaction reading it, so a reader sees one content whole whatever is put meanwhile. A writer fills a
 * content of its own and commits it. A drag's data is a content too (kernel/wm/dnd.c), never committed.
 * Private formats are numbers given to names, the same for everybody until GLOS ends. No locks here: the
 * callers keep each call whole (kernel/app/clip.c). Built for the host too (tests/host/clip_test.c). */
#ifndef K_CLIPSTORE_H
#define K_CLIPSTORE_H
#include "types.h"

#define CLIP_ITEMS    8                 /* formats one content may have */
#define CLIP_NAMES    64                /* private formats there may be */
#define CLIP_NAME_MAX 48                /* a private format's name, with its NUL */
#define CLIP_PRIVATE  0x100u            /* the first private format's number */

struct clip_item {
    u32 format, len;
    void *data;
};

struct clip_content {
    u32 refs;
    u32 serial;                         /* which clipboard it was: the store's count of commits then (0: never one) */
    u32 n;
    struct clip_item item[CLIP_ITEMS];
};

struct clip_host {
    void *(*alloc)(u32 bytes);          /* memory for a content or its data, or NULL */
    void (*free)(void *p, u32 bytes);
};

struct clipstore {
    const struct clip_host *host;
    struct clip_content *now;           /* the clipboard, or NULL: empty */
    u32 serial;                         /* commits so far */
    u32 nnames;
    char names[CLIP_NAMES][CLIP_NAME_MAX];
};

void clipstore_init(struct clipstore *s, const struct clip_host *host);
struct clip_content *clip_new(struct clipstore *s);     /* an empty content, the caller's (one reference); NULL */
/* Room for len bytes of a format in a content not yet committed (what it had of that format is replaced): where
   they go, or NULL (no memory, too many formats, format 0). */
void *clip_add(struct clipstore *s, struct clip_content *c, u32 format, u32 len);
const struct clip_item *clip_find(const struct clip_content *c, u32 format);   /* NULL: it has none (c may be NULL) */
struct clip_content *clip_ref(struct clipstore *s);     /* the clipboard now, kept until clip_unref; NULL: empty */
struct clip_content *clip_keep(struct clip_content *c); /* one more reference to a content (c may be NULL) */
void clip_unref(struct clipstore *s, struct clip_content *c);  /* (c may be NULL) */
void clip_commit(struct clipstore *s, struct clip_content *c); /* c is the clipboard now: the caller's reference
                                                                   passes to the store. NULL: emptied */
u32 clip_format(struct clipstore *s, const char *name); /* a private format's number, given now if the name is new;
                                                           0: a bad name (empty, too long), or no more room */
const char *clip_format_name(const struct clipstore *s, u32 format);    /* NULL: no such private format */

#endif
