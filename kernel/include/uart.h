/* A PC's serial port as a line for bytes (kernel/drv/uart.c): interrupt-driven, a ring each way. One at a time:
   the cable link's (kernel/net/slip.c). */
#ifndef K_UART_H
#define K_UART_H
#include "types.h"

struct uart_counts {
    u32 rx, tx;                         /* bytes */
    u32 overruns;                       /* the chip lost bytes: the kernel came too late for them */
    u32 errors;                         /* framing, parity, a break */
    u32 dropped;                        /* the ring was full */
};

/* com: 1 to 4 (the BIOS's port for it, else the usual one; IRQ 4 for COM1 and COM3, 3 for COM2 and COM4).
   wake_on: kick() is called, from the interrupt, when this byte has come in (and when the ring is half full).
   0; -1: no such port answers; -2: one is open already; -3: a speed the chip can't be set to. From then the
   port and its interrupt are the kernel's: the VM's programs read FFh there, and COM1 carries no log. */
int uart_open(u32 com, u32 baud, u8 wake_on, void (*kick)(void));
void uart_close(void);                  /* the port as it was found, DOS's again */
int uart_is_open(u16 port);             /* 1 if that I/O port is the open line's */
u32 uart_read(u8 *buf, u32 room);       /* what has come in */
u32 uart_room(void);                    /* bytes uart_write() takes now */
void uart_write(const u8 *d, u32 n);    /* queued, and sent as the chip takes them (no more than uart_room()) */
int uart_info(u16 *base, u32 *irq, u32 *baud, int *fifo, struct uart_counts *c);       /* 0, or -1: none open */

#endif
