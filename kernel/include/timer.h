/* The PIC and the kernel clock (supervisor.md §8). */
#ifndef K_TIMER_H
#define K_TIMER_H
#include "types.h"

void pic_init(u8 master_base, u8 slave_base, u16 mask);   /* mask: master bits 0-7, slave 8-15 */
void pic_set_mask(u16 mask);
void timer_start(void);                 /* RTC periodic interrupt at 1024 Hz on IRQ 8 */
void timer_stop(void);                  /* the RTC's registers A and B as found */
u32 timer_ticks(void);
u32 timer_spurious(void);
void pit2_wait(void);                   /* one PIT channel 2 count of FFFFh: 54.925 ms */

#endif
