/* API 2.0's handles (include/glos/api2.h): a table an app, each entry an
 * object of one kind. A handle is the kind in the top byte, a generation in
 * the next, and the entry's index in the low 16 bits, so a closed handle's
 * number is not a newer object's until its entry has gone round 256 times.
 * No locks: the app's own thread and the kernel's tick (interrupts off) are
 * the only users, and the tick only reads. Built for the host too
 * (tests/host/handle_test.c). */
#ifndef K_HANDLE_H
#define K_HANDLE_H
#include "types.h"

#define HANDLE_MAX 256                  /* entries a table (an app's handles at once) */

struct hent {
    u8 kind;                            /* GLOS_HK_, 0 free */
    u8 gen;
    void *obj;
};

struct htab {
    u32 used;                           /* entries in use */
    struct hent e[HANDLE_MAX];
};

void htab_init(struct htab *t);
u32 handle_new(struct htab *t, u8 kind, void *obj);     /* the handle, or 0 if the table is full */
void *handle_obj(const struct htab *t, u32 h, u8 kind); /* its object, if h is a live handle of that kind */
int handle_kind(const struct htab *t, u32 h);           /* its kind, or 0 if h is no live handle */
void *handle_close(struct htab *t, u32 h);              /* gone: its object (for the caller to free), or 0 */
/* Each live handle in turn, from i (0 the first): its object and handle; -1 after the last. */
int handle_next(const struct htab *t, u32 *i, u32 *h, void **obj);

#endif
