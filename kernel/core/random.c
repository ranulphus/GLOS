/* The kernel's random numbers (PRD §7.11; M3 item 4).
 *
 * Interrupts feed timing into a 64-byte buffer with a cheap mix: the TSC
 * where there is one, the tick count and the IRQ number otherwise. When
 * random bytes are asked for, that buffer is folded into a SHA-512 pool
 * together with a counter; the result keys ChaCha20, whose first 32 bytes
 * go back into the pool before the rest is handed out ("fast key erasure":
 * nothing kept can reproduce bytes already given). The loader's seed file
 * (KEYS\SEED.BIN) is mixed in at start, so entropy outlives a reboot; a
 * fresh seed is written back once the DOS server exists (item 9).
 *
 * TinySSH's crypto calls randombytes() under its internal name; so does
 * the SSH server. random_events() lets a caller wait for enough timing
 * (host keys on first boot, PRD D29). */
#include "crypto_hash_sha512.h"
#include "crypto_stream_chacha20.h"
#include "randombytes.h"

#include "glos/bootinfo.h"
#include "io.h"
#include "kprintf.h"
#include "timer.h"

static u8 pool[64];
static u32 fast[16], fast_pos, events, counter;
static int has_tsc;

static u32 irq_off(void)
{
    u32 f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory");
    return f;
}

static void irq_on(u32 f)
{
    if (f & 0x200)
        sti();
}

/* From any context, interrupts on or off: a few cycles. */
void random_event(u32 v)
{
    u32 t = has_tsc ? (u32)rdtsc() : timer_ticks(), i = fast_pos++ & 15;
    fast[i] ^= (t << 7 | t >> 25) ^ v ^ (fast[(i + 5) & 15] << 3);
    fast[(i + 1) & 15] += t;
    events++;
}

u32 random_events(void) { return events; }

static void mix(const void *data, u32 n)
{
    u8 buf[64 + 64 + 256];
    u32 f = irq_off();
    if (n > 256)
        n = 256;
    memcpy(buf, pool, 64);
    memcpy(buf + 64, fast, 64);
    memcpy(buf + 128, data, n);
    memset(fast, 0, sizeof fast);
    irq_on(f);
    crypto_hash_sha512(pool, buf, 128 + n);
    memset(buf, 0, sizeof buf);
}

void random_add(const void *data, u32 n)
{
    const u8 *p = data;
    while (n) {
        u32 k = n > 256 ? 256 : n;
        mix(p, k);
        p += k;
        n -= k;
    }
}

void randombytes(void *out, long long len)
{
    static const u8 zero_nonce[8];
    u8 seed[64], block[256];
    u8 *p = out;
    u32 c = ++counter, k;

    mix(&c, sizeof c);
    while (len > 0) {
        crypto_hash_sha512(seed, pool, 64);     /* a key from the pool, not the pool */
        k = len > (long long)(sizeof block - 32) ? sizeof block - 32 : (u32)len;
        crypto_stream_chacha20(block, 32 + k, zero_nonce, seed);
        mix(block, 32);                         /* the first 32 bytes rekey the pool */
        memcpy(p, block + 32, k);
        p += k;
        len -= k;
    }
    memset(seed, 0, sizeof seed);
    memset(block, 0, sizeof block);
}

const char *randombytes_source(void) { return "glos"; }

void random_init(const struct bootinfo *bi)
{
    has_tsc = (bi->cpuid_edx & 0x10) != 0;
    random_add(bi, sizeof *bi);                 /* the machine's state, and the seed file within */
    kprintf("GLOS-RANDOM seed=%u tsc=%u\n", bi->seed_len, has_tsc);
}
