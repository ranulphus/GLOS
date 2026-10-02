/* The PIC and the kernel clock (supervisor.md §8). */
#ifndef K_TIMER_H
#define K_TIMER_H
#include "types.h"

struct trapframe;

void pic_init(u8 master_base, u8 slave_base, u16 mask);   /* mask: master bits 0-7, slave 8-15 */
void pic_set_mask(u16 mask);
void timer_start(void);                 /* RTC periodic interrupt at 1024 Hz on IRQ 8 */
void timer_stop(void);                  /* the RTC's registers A and B as found */
void timer_stop_to(u8 a, u8 b);         /* ... or as given */
void timer_set_hook(void (*fn)(struct trapframe *tf, u8 c));     /* every tick, with register C */
u8 timer_found_a(void);
u8 timer_found_b(void);
u8 rtc_read(u8 reg);                    /* CMOS/RTC registers, NMI left enabled */
void rtc_write(u8 reg, u8 v);
void timer_write_b(u8 b);               /* register B's time-keeping bits; PIE stays on */
u32 timer_ticks(void);
u32 timer_spurious(void);
void pit2_wait(void);                   /* one PIT channel 2 count of FFFFh: 54.925 ms */

#endif
