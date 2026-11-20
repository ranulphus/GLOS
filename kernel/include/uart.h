/* A PC's serial port as a line for bytes (kernel/drv/uart.c): interrupt-driven, a ring each way. Two at most:
   the cable link's (kernel/net/slip.c: uart_*), and an app's (API 2.10, NET's SERIAL: uart_app_*). */
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

/* An app's line (API 2.10): the frame and the flow control a terminal on a wire may want. */
enum { UART_PARITY_NONE, UART_PARITY_ODD, UART_PARITY_EVEN };
enum { UART_FLOW_NONE, UART_FLOW_RTS, UART_FLOW_XON };  /* none; the RTS and CTS wires; XON and XOFF in the bytes */
#define UART_LINE_DTR 1         /* this side's two ... */
#define UART_LINE_RTS 2
#define UART_LINE_CTS 4         /* ... and the far side's */
#define UART_LINE_DSR 8
#define UART_LINE_DCD 16
#define UART_LINE_RI  32
struct uart_setup {
    u32 com, baud;              /* COM1 to COM4; 50 to 115200, as near as the chip's divisor comes (within a hundredth) */
    u32 bits, parity, stop;     /* 5 to 8; UART_PARITY_; 1 or 2 */
    u32 flow;                   /* UART_FLOW_ */
};
/* 0; -1: no such port answers; -2: a line is open already, or the cable link has that port; -3: a speed or a
   frame the chip can't have; -4: no memory. kick() is called from the interrupt when bytes have come, and when
   there is room to send again. COM1 carries no log while it is open. */
int uart_app_open(const struct uart_setup *su, void (*kick)(void));
void uart_app_close(void);
int uart_app_on(void);
u32 uart_app_read(u8 *buf, u32 room);
u32 uart_app_room(void);
void uart_app_write(const u8 *d, u32 n);
int uart_app_idle(void);        /* 1: all that was written has gone out of the chip */
u32 uart_app_lines(void);       /* UART_LINE_s */
void uart_app_set(u32 dtr, u32 rts);
void uart_app_break(int on);

#endif
