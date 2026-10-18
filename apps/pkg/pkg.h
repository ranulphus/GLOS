/* PKG (M10 step E): what its parts give each other. */
#ifndef PKG_H
#define PKG_H
#include <stddef.h>
#include <stdio.h>

#define ZIP_MAX 512

struct zip_entry {
    char name[80];                      /* as DOS has it: "BIN\\NOTES.EXE" */
    int dir;
    unsigned method;
    unsigned long crc, csize, usize, offset;
};

struct zip {
    const unsigned char *data;
    size_t size;
    struct zip_entry e[ZIP_MAX];
    int n;
};

unsigned long pkg_crc32(unsigned long crc, const void *data, size_t n);
int zip_open(struct zip *z, const unsigned char *data, size_t size, char *why, size_t room);
const struct zip_entry *zip_find(const struct zip *z, const char *name);
unsigned char *zip_read(const struct zip *z, const struct zip_entry *e, char *why, size_t room);   /* malloc'd */

/* pkg.c, for the window */
void pkg_say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int pkg_count(void);
void pkg_row(int i, const char **cells, char *inst, size_t room);
void pkg_reload(void);
int pkg_install(const char *name);
int pkg_remove(const char *name);
int pkg_upgrade_all(void);
/* win.c */
int pkg_window(int argc, char **argv);
void pkg_status(const char *text);

#endif
