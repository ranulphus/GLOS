/* MIDI messages in time (API 2.0's AUDIO MIDI_OUT and MIDI_IN; docs/v1/phase-b.md step 10): the queue a port out
 * plays from on the kernel's tick, and the parser of what a port in receives. Times are milliseconds on GLOS's
 * clock, compared as signed differences (the clock wraps after 49 days). */
#include "midiq.h"

u32 midi_len(u8 status)
{
    if (status < 0x80 || status >= 0xF0)
        return 0;
    return (status & 0xE0) == 0xC0 ? 2 : 3;     /* (program change and channel pressure: one data byte) */
}

int midiq_put(struct midiq *q, const struct midi_msg *m, u32 now)
{
    struct midi_msg x = *m;
    u32 i;
    if (!midi_len(x.status))
        return -2;
    if (q->n >= MIDIQ_MAX)
        return -1;
    if (!x.time || (s32)(x.time - now) < 0)
        x.time = now;
    for (i = q->n; i > 0; i--) {                /* (from the end: a player's messages come in order) */
        struct midi_msg *prev = &q->m[(q->head + i - 1) % MIDIQ_MAX];
        if ((s32)(prev->time - x.time) <= 0)
            break;
        q->m[(q->head + i) % MIDIQ_MAX] = *prev;
    }
    q->m[(q->head + i) % MIDIQ_MAX] = x;
    q->n++;
    return 0;
}

int midiq_due(struct midiq *q, u32 now, struct midi_msg *out)
{
    if (!q->n || (s32)(q->m[q->head].time - now) > 0)
        return 0;
    *out = q->m[q->head];
    q->head = (q->head + 1) % MIDIQ_MAX;
    q->n--;
    return 1;
}

void midiq_clear(struct midiq *q) { q->head = q->n = 0; }

int midi_parse(struct midi_parse *p, u8 b, struct midi_msg *out)
{
    if (b >= 0xF8)                              /* real-time (the clock, active sensing): between any two bytes */
        return 0;
    if (b >= 0x80) {
        p->have = 0;
        p->sysex = b == 0xF0;
        p->status = b < 0xF0 ? b : 0;           /* (a system message ends the running status; its data is dropped) */
        return 0;
    }
    if (p->sysex || !p->status)
        return 0;
    if (midi_len(p->status) == 3 && !p->have) {
        p->d1 = b;
        p->have = 1;
        return 0;
    }
    out->status = p->status;
    out->d1 = midi_len(p->status) == 3 ? p->d1 : b;
    out->d2 = midi_len(p->status) == 3 ? b : 0;
    out->pad = 0;
    p->have = 0;                                /* (the status runs on: the next data bytes are its again) */
    return 1;
}
