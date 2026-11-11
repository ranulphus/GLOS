/* MIDI messages in time (kernel/drv/midiq.c): the queue against the clock, and the input's parser. */
#include <stdio.h>
#include <string.h>

#include "midiq.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("midiq_test: line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static struct midiq q;

static int put(u32 time, u8 status, u8 d1, u32 now)
{
    struct midi_msg m = { time, status, d1, 0, 0 };
    return midiq_put(&q, &m, now);
}

static int feed(struct midi_parse *p, const u8 *b, u32 n, struct midi_msg *out, u32 max)
{
    u32 i, got = 0;
    for (i = 0; i < n; i++) {
        struct midi_msg m;
        memset(&m, 0xEE, sizeof m);
        if (midi_parse(p, b[i], &m) && got < max)
            out[got++] = m;
    }
    return (int)got;
}

int main(void)
{
    struct midi_msg m, out[8];
    struct midi_parse p;
    u32 i, t, played;

    CHECK(midi_len(0x90) == 3 && midi_len(0x8F) == 3 && midi_len(0xB3) == 3 && midi_len(0xE0) == 3);
    CHECK(midi_len(0xC0) == 2 && midi_len(0xD5) == 2 && midi_len(0xF0) == 0 && midi_len(0x7F) == 0 && midi_len(0xF8) == 0);

    /* Each plays at its time and not a tick before, in time's order whatever order they came in; equal times in
       the order they came. */
    CHECK(put(1300, 0x90, 3, 1000) == 0 && put(1100, 0x90, 1, 1000) == 0 && put(1200, 0x90, 2, 1000) == 0);
    CHECK(put(1200, 0x80, 2, 1000) == 0 && put(1100, 0xC0, 9, 1000) == 0);
    CHECK(q.n == 5 && !midiq_due(&q, 1000, &m) && !midiq_due(&q, 1099, &m));
    CHECK(midiq_due(&q, 1100, &m) && m.status == 0x90 && m.d1 == 1 && m.time == 1100);
    CHECK(midiq_due(&q, 1100, &m) && m.status == 0xC0 && m.d1 == 9);
    CHECK(!midiq_due(&q, 1100, &m) && !midiq_due(&q, 1199, &m));
    CHECK(midiq_due(&q, 1250, &m) && m.status == 0x90 && m.d1 == 2);        /* (a late look: still in order) */
    CHECK(midiq_due(&q, 1250, &m) && m.status == 0x80 && m.d1 == 2);
    CHECK(!midiq_due(&q, 1250, &m) && midiq_due(&q, 1300, &m) && m.d1 == 3 && q.n == 0 && !midiq_due(&q, 9999, &m));

    /* A time of 0, or one gone by, is now: before what waits for later, after what is due already. */
    CHECK(put(5000, 0x90, 1, 4000) == 0 && put(0, 0x90, 2, 4000) == 0 && put(3000, 0x90, 3, 4000) == 0);
    CHECK(midiq_due(&q, 4000, &m) && m.d1 == 2 && m.time == 4000);
    CHECK(midiq_due(&q, 4000, &m) && m.d1 == 3 && m.time == 4000);
    CHECK(!midiq_due(&q, 4999, &m) && midiq_due(&q, 5000, &m) && m.d1 == 1);

    /* The clock's wrap: a time just past it is later than one just before. */
    CHECK(put(5, 0x90, 2, 0xFFFFFFF0u) == 0 && put(0xFFFFFFF8u, 0x90, 1, 0xFFFFFFF0u) == 0);
    CHECK(!midiq_due(&q, 0xFFFFFFF7u, &m) && midiq_due(&q, 0xFFFFFFF8u, &m) && m.d1 == 1);
    CHECK(!midiq_due(&q, 0xFFFFFFFFu, &m) && !midiq_due(&q, 4, &m) && midiq_due(&q, 5, &m) && m.d1 == 2);

    /* Only channel messages; a full queue refuses, and what it has still plays in order (the ring goes round). */
    CHECK(put(0, 0xF0, 0, 100) == -2 && put(0, 0x40, 0, 100) == -2 && put(0, 0xF8, 0, 100) == -2 && q.n == 0);
    for (i = 0; i < 300; i++)
        CHECK(put(100 + i, 0x90, (u8)(i & 127), 100) == 0);
    for (i = 0; i < 300; i++)
        CHECK(midiq_due(&q, 1000, &m) && m.time == 100 + i);
    for (i = 0; i < MIDIQ_MAX; i++)
        CHECK(put(2000 + (i ^ 1), 0x90, (u8)(i & 127), 1000) == 0);    /* (pairs the wrong way round) */
    CHECK(put(2000, 0x90, 0, 1000) == -1 && q.n == MIDIQ_MAX);
    for (i = 0; i < MIDIQ_MAX; i++)
        CHECK(midiq_due(&q, 5000, &m) && m.time == 2000 + i);
    CHECK(q.n == 0);

    /* Against a tick: a second of notes 10 ms apart, looked at every millisecond, each played in the millisecond
       it is due. */
    for (i = 0; i < 100; i++)
        CHECK(put(10000 + 10 * i, 0x90, (u8)i, 9000) == 0);
    for (t = 9990, played = 0; t < 11010; t++)
        while (midiq_due(&q, t, &m)) {
            CHECK(m.time == t && m.d1 == played);
            played++;
        }
    CHECK(played == 100);
    CHECK(put(0, 0x90, 1, 0) == 0);
    midiq_clear(&q);
    CHECK(q.n == 0 && !midiq_due(&q, 99999, &m));

    /* The parser: whole messages; the running status; one data byte for a program change. */
    memset(&p, 0, sizeof p);
    {
        static const u8 b[] = { 0x90, 60, 100, 62, 101, 0x80, 60, 0, 0xC1, 5, 6, 0xE2, 0x00, 0x40 };
        CHECK(feed(&p, b, sizeof b, out, 8) == 6);
        CHECK(out[0].status == 0x90 && out[0].d1 == 60 && out[0].d2 == 100 && out[0].pad == 0);
        CHECK(out[1].status == 0x90 && out[1].d1 == 62 && out[1].d2 == 101);
        CHECK(out[2].status == 0x80 && out[2].d1 == 60 && out[2].d2 == 0);
        CHECK(out[3].status == 0xC1 && out[3].d1 == 5 && out[3].d2 == 0);
        CHECK(out[4].status == 0xC1 && out[4].d1 == 6);
        CHECK(out[5].status == 0xE2 && out[5].d1 == 0 && out[5].d2 == 0x40);
    }
    /* Real-time bytes between a message's bytes change nothing; data with no status is dropped; a system
       exclusive message is dropped to its end, and ends the running status; so does a system common one. */
    memset(&p, 0, sizeof p);
    {
        static const u8 b[] = { 1, 2, 3, 0x90, 0xF8, 60, 0xFE, 100, 0xF0, 0x7E, 1, 2, 0xF8, 3, 0xF7, 61, 100,
                                0x91, 61, 0xF2, 1, 2, 62, 0x92, 63, 0x92, 64, 65 };
        CHECK(feed(&p, b, sizeof b, out, 8) == 2);
        CHECK(out[0].status == 0x90 && out[0].d1 == 60 && out[0].d2 == 100);
        CHECK(out[1].status == 0x92 && out[1].d1 == 64 && out[1].d2 == 65);     /* (a new status starts it anew) */
    }

    if (fails)
        return 1;
    printf("midiq_test: ok\n");
    return 0;
}
