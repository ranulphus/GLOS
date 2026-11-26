/* sdk/fm's driver half (include/glos/fmsynth.h): the queue of messages in time (kernel/drv/midiq.c) and the
 * driver (kernel/drv/fm.c) joined, in the kernel's own types, for fmsynth.c, which speaks plain C's. Compiled as
 * the kernel's files are, freestanding with kernel/include. */
#include "fm.h"
#include "midiq.h"

static struct midiq q;

int fmq_put(u32 at, u8 status, u8 d1, u8 d2, u32 now)
{
    struct midi_msg m;
    m.time = at ? at : now;
    m.status = status, m.d1 = d1, m.d2 = d2, m.pad = 0;
    return midiq_put(&q, &m, now) == 0 ? 0 : -1;
}

void fmq_play(u32 now)
{
    struct midi_msg m;
    while (midiq_due(&q, now, &m))
        fm_message(m.status, m.d1, m.d2);
}

u32 fmq_n(void) { return q.n; }

void fmq_clear(void) { midiq_clear(&q); }

u32 fmq_len(u8 status) { return midi_len(status); }
