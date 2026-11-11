/* The secrets store (kernel/core/secrets.c; API 2.0's SECRETS, phase B step 13): its file beside GLOS.CFG, its
 * key (the PC's own, or one derived from a master password), and what apps and `glos secrets` ask of it. */
#ifndef K_SECRETS_H
#define K_SECRETS_H
#include "types.h"

enum { SEC_NONE, SEC_OPEN, SEC_LOCKED, SEC_BAD };       /* no store here (no agent's DOS server); usable; a master
                                                           password is wanted first; its file can't be read */
enum { SEC_E_NONE = -1, SEC_E_LOCKED = -2, SEC_E_ROOM = -3, SEC_E_LONG = -4, SEC_E_WRONG = -5, SEC_E_BAD = -6 };

/* The config thread (kernel/core/config.c), the DOS server held. */
void secrets_init(const char *cfg_path);
void secrets_load(void);
int secrets_dirty(void);
int secrets_save(void);

/* Any thread that may wait. owner: a program's file name, or "*" for a shared secret. 0, or SEC_E_. */
int secrets_state(void);
int secrets_put(const char *owner, const char *name, const u8 *value, u32 len);
int secrets_get(const char *owner, const char *name, u8 *out, u32 room, u32 *len);
int secrets_remove(const char *owner, const char *name);
int secrets_grant(const char *prog, const char *name, int on); /* the user lets prog use the shared secret (or no more) */
int secrets_granted(const char *prog, const char *name);        /* 1 if so */
int secrets_unlock(const char *password);       /* 0; SEC_E_WRONG; SEC_E_NONE: not locked */
int secrets_password(const char *old, const char *now);         /* the master password set, changed (old: the one it
                                                   has) or, with now empty, taken away: 0; SEC_E_WRONG; SEC_E_LOCKED */
int secrets_has_password(void);
u32 secrets_stat(char *buf, u32 max);           /* glos secrets */

#endif
