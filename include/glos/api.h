/* The GLOS API, version 1 (PRD §10). M6 builds it up: what is here is what
 * exists.
 *
 * A program finds the API with DPMI function 0A00h and the name "GLOS"
 * (INT 31h, AX=0A00h, DS:ESI the name): ES:EDI comes back as the entry, a
 * far procedure. Plain DOS and other DPMI hosts have no such name, so a
 * program that gets no answer runs as it always did. A function is called
 * by its number in EAX, with its arguments in EBX, ECX, EDX, ESI and EDI,
 * and returns in the same registers; the carry flag is set for an error,
 * whose GLOS_E_ code is then in EAX. Numbers are never reused, and new
 * functions are added at the end. Structures passed by address start with
 * their own size.
 *
 * A program becomes a GLOS app with GLOS_FN_INIT: from then on it runs
 * beside other apps on a thread of its own, and the rest of the API is
 * open to it. Until then it is an exclusive session, as any DOS program.
 *
 * sdk/libglos is the C side of this for DJGPP (glos_init() and the rest). */
#ifndef GLOS_API_H
#define GLOS_API_H

#define GLOS_API_MAJOR   1
#define GLOS_API_MINOR   0
#define GLOS_API_VERSION ((GLOS_API_MAJOR << 16) | GLOS_API_MINOR)

/* Functions (EAX). */
#define GLOS_FN_VERSION  0      /* -> EAX the API's version (major << 16 | minor), EBX capability bits (none yet),
                                   ECX the number of functions. For any program. */
#define GLOS_FN_INIT     1      /* EBX the version the program was built for (its major must be the API's)
                                   -> EAX 0, EBX the app's id. The program is a GLOS app. Refused with
                                   GLOS_E_INIT and a GLOS_INIT_ reason in EBX: the program runs on as a session. */
#define GLOS_FN_YIELD    2      /* the rest of this turn to other threads */
#define GLOS_FN_SLEEP    3      /* EBX milliseconds */
#define GLOS_FN_TICKS    4      /* -> EAX milliseconds since GLOS started (it wraps after 49 days) */
#define GLOS_FN_COUNT    5

/* Errors (EAX, with the carry flag). */
#define GLOS_E_FUNCTION  1      /* no such function */
#define GLOS_E_NOT_APP   2      /* for apps only: GLOS_FN_INIT first (or called from a real-mode callback) */
#define GLOS_E_ARGUMENT  3
#define GLOS_E_VERSION   4      /* GLOS_FN_INIT: the program wants another major version */
#define GLOS_E_INIT      5      /* GLOS_FN_INIT: refused, EBX says why */

/* Why GLOS_FN_INIT was refused (EBX). */
#define GLOS_INIT_ALREADY   1   /* it is an app already */
#define GLOS_INIT_NESTED    2   /* called by a program another program started, or from a handler or a callback */
#define GLOS_INIT_16BIT     3   /* a 16-bit DPMI client */
#define GLOS_INIT_PARENT    4   /* GLOS didn't start the program itself (a batch file did, or COMMAND.COM) */
#define GLOS_INIT_EXCLUSIVE 5   /* it was started to run as an exclusive session (glos run --exclusive, direct mode) */
#define GLOS_INIT_MEMORY    6   /* no memory for another app, or too many */

/* sdk/libglos (DJGPP). */
#ifndef GLOS_KERNEL
#ifdef __cplusplus
extern "C" {
#endif
unsigned glos_version(void);    /* the API's version, 0 without GLOS */
int glos_init(void);            /* become an app: 0; -1 without GLOS or when refused (glos_error()) */
int glos_error(void);           /* the last failed call's GLOS_E_ code (0: there is no GLOS) ... */
int glos_reason(void);          /* ... and for GLOS_E_INIT, the GLOS_INIT_ reason */
unsigned glos_id(void);         /* the app's id, 0 before glos_init() */
void glos_yield(void);
void glos_sleep(unsigned ms);
unsigned glos_ticks(void);      /* milliseconds since GLOS started */
#ifdef __cplusplus
}
#endif
#endif

#endif
