/* GLOS.CFG in the kernel (kernel/core/config.c; include/glos/config.h). */
#ifndef K_KCFG_H
#define K_KCFG_H
#include "types.h"

struct bootinfo;
struct bi_profile;

void kcfg_init(const struct bootinfo *bi);
int kcfg_loaded(void);
int kcfg_get(const char *section, const char *key, char *out, u32 room);   /* 1 set, 0 the default, -1 no such */
int kcfg_set(const char *section, const char *key, const char *value, char *why, u32 room);    /* CFG_ when, or -1;
                                           value NULL: unset */
int kcfg_profile(const char *name, struct bi_profile *p);       /* 0; -1 none; -2 the file not read (bi's) */
u32 kcfg_list(char *buf, u32 max);
u32 kcfg_sections(char *buf, u32 max);
u32 kcfg_stat(char *buf, u32 max);
void kcfg_apply(const char *section, const char *key, const char *value);   /* a setting in effect now */
void kcfg_on_change(void (*apply)(const char *section, const char *key, const char *value));

#endif
