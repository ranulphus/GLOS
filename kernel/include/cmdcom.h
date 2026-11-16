/* GLOS's COMMAND.COM, the kernel's side (kernel/shell/cmdcom.c; docs/v1/shell.md, step S4c). */
#ifndef K_CMDCOM_H
#define K_CMDCOM_H
#include "types.h"

struct trapframe;
struct shsess;

int cmdcom_call(struct trapframe *tf);  /* (vm/v86.c) an invalid opcode in V86 code: 1 if it was a COMMAND.COM's
                                           call, answered */
int cmdcom_waits(u16 cs);               /* a HLT at this segment is the COMMAND.COM on top's */
int cmdcom_pending(void);               /* ... which has something to do: its wait ends */
void cmdcom_stub_asks(void);            /* the resident stub asks what next: no COMMAND.COM runs any more */
void cmdcom_killed(void);               /* the VM's program was killed */
const char *cmdcom_path(void);          /* GLOS's own COMMAND.COM for the programs GLOS starts, or 0: the DOS's */

/* For the session (shsess.c), on its thread. */
int cmdcom_exec(void *via, struct shsess *sess, const char *path, const char *tail, char *const *vars, int nvars);
int cmdcom_done(void *via, struct shsess *sess, int *code);
int cmdcom_clear(void *via, struct shsess *sess);       /* CLS: the PC's screen, by its COMMAND.COM */
int cmdcom_cleared(void *via, struct shsess *sess);

#endif
