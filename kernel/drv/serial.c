/* 16550 UARTs, polled, 115200 8N1: COM1 for GLOS- lines (written
   synchronously, so the last line survives a hang), COM2 for the gdb stub. */
#include "io.h"
#include "kprintf.h"

void serial_init(u16 port)
{
    outb(port + 1, 0x00);                       /* no interrupts */
    outb(port + 3, 0x80);
    outb(port + 0, 0x01);                       /* divisor 1: 115200 */
    outb(port + 1, 0x00);
    outb(port + 3, 0x03);                       /* 8N1 */
    outb(port + 2, 0xC7);
    outb(port + 4, 0x03);
}

void serial_putc(u16 port, char c)
{
    int n = 0;
    while (!(inb(port + 5) & 0x20) && ++n < 100000) ;
    outb(port, (u8)c);
}

int serial_getc(u16 port)
{
    return (inb(port + 5) & 1) ? inb(port) : -1;
}
