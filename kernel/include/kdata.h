/* GLOS.DAT (beside GLOS.CFG): what GLOS keeps for itself that is not a setting: the recent documents and the
 * schedule (API 2.0's SHELL RECENT and SCHEDULE; docs/v1/phase-b.md step 8). Lines of text, each a kind (a
 * word), a space, and a record; the records of a kind are in the order they are kept. The list is plain code,
 * built for the host too (tests/host/kdata_test.c); the kernel's store, with its file, is below it. */
#ifndef K_KDATA_H
#define K_KDATA_H
#include "types.h"

#define KDATA_MAX  64                   /* records, of every kind */
#define KDATA_TEXT 240                  /* a record's bytes at most, without its kind */
#define KDATA_KIND 8                    /* a kind's name, with its NUL */

struct kdata {
    char line[KDATA_MAX][KDATA_KIND + KDATA_TEXT + 2];
    u32 n;
};

u32 kd_count(const struct kdata *d, const char *kind);
/* The n-th record of a kind (0 the first) into out: its length, or -1 (no such record, or no room). */
int kd_get(const struct kdata *d, const char *kind, u32 n, char *out, u32 room);
/* A record added: the first of its kind (front) or the last. 0, or -1: no room, too long, or a line end in it. */
int kd_add(struct kdata *d, const char *kind, const char *text, int front);
int kd_remove(struct kdata *d, const char *kind, u32 n);       /* 0, or -1 */
int kd_set(struct kdata *d, const char *kind, u32 n, const char *text);        /* the record changed in place */
u32 kd_write(const struct kdata *d, char *buf, u32 room);      /* the file's text: its length */
void kd_read(struct kdata *d, const char *buf, u32 len);       /* ... read (lines that are no records are dropped) */

/* The kernel's (interrupts off around each: any thread). */
void kdata_init(const char *cfg_path);
u32 kdata_count(const char *kind);
int kdata_get(const char *kind, u32 n, char *out, u32 room);
int kdata_add(const char *kind, const char *text, int front);
int kdata_remove(const char *kind, u32 n);
int kdata_set(const char *kind, u32 n, const char *text);
u32 kdata_serial(void);                 /* counts its changes */
/* The file, on the settings' writer thread (kernel/core/config.c), which holds the DOS server meanwhile. */
void kdata_load(void);
int kdata_dirty(void);
int kdata_save(void);                   /* 0, or -1 */
void kcfg_writer_kick(void);            /* (config.c) the writer looks again */

#endif
