/* ISA Plug and Play in the kernel (kernel/drv/pnpbus.c over isapnp.c). */
#ifndef K_PNPBUS_H
#define K_PNPBUS_H

#include "types.h"
#include "../drv/isapnp.h"

struct pnp_dev *pnpbus_find(unsigned long vendor, unsigned io, unsigned irq, unsigned dma, const struct pnp_dev *after);
int pnpbus_on(struct pnp_dev *d);       /* on as it is, or set up: 0 */
u32 pnpbus_stat(char *b, u32 room);     /* glos pnp */
int pnpbus_io_busy(u16 lo, u16 hi);     /* a device that is on holds a port of these: 1 */

#endif
