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
u32 session_memory_kb(void);                    /* the session's profile's memory cap, 0 none */
int session_direct(void);                       /* direct mode: its profile, glos run --direct or /DIRECT */
void session_job(int direct, int exclusive, const char *profile);      /* glos run's options for the stub's next EXEC */
int session_profile_known(const char *name);    /* GLOS.CFG has [program NAME] */
u32 session_stat(char *buf, u32 max);           /* glos stat: the session as a JSON object, or null */
#endif
