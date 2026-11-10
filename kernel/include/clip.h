/* The clipboard (kernel/app/clip.c over kernel/app/clipstore.c): the system's, one for every app and the agent.
 * API 1.x and the agent see its text (GLOS_FN_CLIP, glos clip); API 2.0 every format (CLIP, api2clip.c). */
#ifndef K_CLIP_H
#define K_CLIP_H
#include "types.h"

#define CLIP_MAX 65536u                         /* 1.x's and the agent's text at most */
#define CLIP_TEXT 1u                            /* (GLOS_CLIP_TEXT) */

int clip_set(const char *text, u32 n);          /* 0, or -1 (too long, no memory) */
int clip_set_user(u16 sel, u32 off, u32 n);     /* ... from an app's memory */
u32 clip_get(char *out, u32 room);              /* the clipboard's text's bytes; as many as fit copied */
int clip_get_user(u16 sel, u32 off, u32 room, u32 *len);       /* ... into an app's memory: 0, or -1 */

/* Contents (clipstore.h), each call whole. A content's data is the caller's to fill until it is committed, and
   anybody's to read while it holds a reference. */
struct clip_content;
struct clip_item;
struct clip_content *clip_content_new(void);
void *clip_content_add(struct clip_content *c, u32 format, u32 len);
struct clip_content *clip_content_now(void);    /* the clipboard, kept until clip_content_drop; NULL: empty */
struct clip_content *clip_content_keep(struct clip_content *c);
void clip_content_drop(struct clip_content *c);
void clip_content_commit(struct clip_content *c);       /* it is the clipboard now (the caller's reference passes on) */
u32 clip_serial(void);                          /* how many times the clipboard has been changed */
u32 clip_private(const char *name);             /* a private format's number, 0 if it can't have one */
void clip_hook(void (*changed)(u32 serial));    /* who is told of each change */

#endif
