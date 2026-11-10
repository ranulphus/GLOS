/* The virtual game port (docs/gamepad.md J1): port 201h and INT 15h AH=84h answered by GLOS while a joystick
 * source is on. */
#ifndef K_JOY_H
#define K_JOY_H
#include "types.h"

struct trapframe;

#define JOY_SRC_SSH     0               /* glos joy */
#define JOY_SOURCES     1               /* (USB pads join in J4-J6) */

struct joy_state {
    u8 axis[4];                         /* joystick A's X, Y, then B's: 0-255, 128 the centre */
    u8 present;                         /* bit n: axis n is connected */
    u8 buttons;                         /* bits 0-3: A1, A2, B1, B2, 1 while pressed */
};

/* A USB pad's state (kernel/drv/uhci.c): its sticks (the D-pad folded into the left one) and buttons. */
struct joy_pad {
    u8 lx, ly, rx, ry;                  /* 0-255, 128 the centre */
    u8 right;                           /* it has a right stick */
    u16 buttons;                        /* bit n: button n + 1 */
};

#define JOY_PADS        2

void joy_pad(int n, const struct joy_pad *p);   /* USB pad n (0, 1); NULL: unplugged */
int joy_pad_get(int n, struct joy_pad *out);    /* pad n as it is now: 1, or 0 if there is none */
void joy_pad_hook(void (*fn)(int n, const struct joy_pad *p)); /* told of every joy_pad() (API 2.0's INPUT) */
void joy_info(int *on, int *mode, u32 *range_us, u32 *pads);    /* for API 2.0's GAMEPORT */
void joy_config(void);                  /* [joystick] port and range, from GLOS.CFG */
void joy_source(int src, const struct joy_state *s);     /* a source's state; NULL: the source is gone */
int joy_virtual(void);                  /* 201h is GLOS's now (else the PC's own, passed through) */
u8 joy_in(void);                        /* a read of 201h */
void joy_out(void);                     /* a write: the one-shots fire */
int joy_int15(struct trapframe *tf, u32 ret_ip);         /* INT 15h AH=84h; 0: the BIOS's */
void joy_tick(u32 now);                 /* every kernel tick: the TSC's rate, IRQ 0's */
int joy_stat(char *b, u32 room);        /* glos joy */
int joy_command(const char *args, char *why, u32 room);  /* glos joy a=X,Y b=X,Y buttons=NNNN | off */

#endif
