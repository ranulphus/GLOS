/* Exclusive sessions (supervisor.md §11; kernel/vm/session.c). */
#ifndef K_SESSION_H
#define K_SESSION_H
#include "arch.h"

void session_exec(const struct trapframe *tf);  /* INT 21h 4B00h from V86 code */
void session_terminate(void);
void session_kill(u8 mode, u8 leds);            /* after a kill's (or abort's) snapshot restore (E5) */                   /* INT 20h, INT 21h 4Ch or 00h from V86 code */
void session_exit_code(void);                   /* INT 21h 4Dh from V86 code */
void session_stub_next(void);                   /* GLOS.EXE's stub's NEXT */
int session_active(void);
const char *session_prog(void);                 /* the session's program's file name */
const u32 *session_ivt(void);                   /* the real-mode vectors as the session found them */
int session_exclusive(void);                    /* glos run --exclusive: it stays a session (app.c) */
int session_app(void);                          /* glos run --app: its program is to be an app without asking */
int session_console(void);                      /* a console job's program runs in its window (M9) */
void session_console_full(void);                /* ... and wants the screen */
void session_video(void);                       /* the program in front set a video mode */
int session_pauses(void);                       /* GLOS apps wait for it (not for a program marked as an app, while
                                                   it starts; its end's work) */
u32 session_memory_kb(void);                    /* the session's profile's memory cap, 0 none */
int session_direct(void);                       /* direct mode: its profile, glos run --direct or /DIRECT */
void session_job(int direct, int how, const char *profile);     /* glos run's options for the stub's next EXEC
                                                   (how: 0, 1 --exclusive, 2 --app) */
int session_profile_known(const char *name);    /* GLOS.CFG has [program NAME] */
u32 session_stat(char *buf, u32 max);           /* glos stat: the session as a JSON object, or null */
#endif
