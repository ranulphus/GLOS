/* gdb remote serial protocol on a UART, polled: enough for gdb to attach,
 * read and write registers and memory, set software breakpoints, single-step
 * and continue (supervisor.md §19). It owns #DB and #BP once started. Memory
 * goes through the fault-protected helpers, so a bad address is an E01, not
 * a crash. */
#include "arch.h"
#include "gdb.h"
#include "kprintf.h"

#define EF_TF 0x100u
#define NBP 16

static u16 port;
static int resumed;                     /* gdb is waiting for a stop reply */
static char pkt[512], out[512];
static struct { u32 addr; u8 saved; int on; } bp[NBP];

static const char hexd[] = "0123456789abcdef";

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int getc_wait(void)
{
    int c;
    while ((c = serial_getc(port)) < 0) ;
    return c;
}

/* A packet's payload, acknowledged; NUL-terminated in pkt. */
static void recv_packet(void)
{
    for (;;) {
        int c, n = 0;
        u8 sum = 0;
        while ((c = getc_wait()) != '$') ;
        while ((c = getc_wait()) != '#' && n < (int)sizeof pkt - 1) {
            pkt[n++] = (char)c;
            sum += (u8)c;
        }
        pkt[n] = 0;
        c = hexv((char)getc_wait()) << 4;
        c |= hexv((char)getc_wait());
        if (c == sum) { serial_putc(port, '+'); return; }
        serial_putc(port, '-');
    }
}

static void send_packet(const char *s)
{
    u8 sum;
    const char *p;
    do {
        serial_putc(port, '$');
        for (sum = 0, p = s; *p; p++) { serial_putc(port, *p); sum += (u8)*p; }
        serial_putc(port, '#');
        serial_putc(port, hexd[sum >> 4]);
        serial_putc(port, hexd[sum & 15]);
    } while (getc_wait() != '+');
}

static char *put_hex32(char *o, u32 v)          /* little-endian, as gdb wants registers */
{
    int i;
    for (i = 0; i < 4; i++, v >>= 8) { *o++ = hexd[(v >> 4) & 15]; *o++ = hexd[v & 15]; }
    return o;
}

static const char *get_hex(const char *s, u32 *v)
{
    int d;
    *v = 0;
    while ((d = hexv(*s)) >= 0) { *v = (*v << 4) | (u32)d; s++; }
    return s;
}

static int read_byte(u32 a, u8 *b)
{
    u32 w;
    if (try_rd32(a & ~3u, &w)) return -1;
    *b = (u8)(w >> ((a & 3) * 8));
    return 0;
}

/* gdb's i386 order: eax ecx edx ebx esp ebp esi edi eip eflags cs ss ds es fs gs */
static void regs_out(struct trapframe *tf)
{
    u32 esp = (u32)&tf->esp;                    /* ring 0: no ESP/SS were pushed */
    u32 r[16] = { tf->eax, tf->ecx, tf->edx, tf->ebx, esp, tf->ebp, tf->esi, tf->edi,
                  tf->eip, tf->eflags, tf->cs, 0x10, tf->ds, tf->es, tf->fs, tf->gs };
    char *o = out;
    int i;
    for (i = 0; i < 16; i++) o = put_hex32(o, r[i]);
    *o = 0;
}

static void regs_in(struct trapframe *tf, const char *s)
{
    u32 r[16], i;
    for (i = 0; i < 16; i++) {
        u32 v = 0, k;
        for (k = 0; k < 4 && s[0] && s[1]; k++, s += 2)
            v |= (u32)((hexv(s[0]) << 4) | hexv(s[1])) << (8 * k);
        r[i] = v;
    }
    tf->eax = r[0]; tf->ecx = r[1]; tf->edx = r[2]; tf->ebx = r[3];
    tf->ebp = r[5]; tf->esi = r[6]; tf->edi = r[7]; tf->eip = r[8]; tf->eflags = r[9];
}

static int set_bp(u32 a, int on)
{
    int i, free_slot = -1;
    for (i = 0; i < NBP; i++) {
        if (bp[i].on && bp[i].addr == a) {
            if (on) return 0;
            bp[i].on = 0;
            return try_wr8(a, bp[i].saved) ? -1 : 0;
        }
        if (!bp[i].on && free_slot < 0) free_slot = i;
    }
    if (!on) return 0;
    if (free_slot < 0 || read_byte(a, &bp[free_slot].saved) || try_wr8(a, 0xCC)) return -1;
    bp[free_slot].addr = a;
    bp[free_slot].on = 1;
    return 0;
}

static int is_bp(u32 a)
{
    int i;
    for (i = 0; i < NBP; i++)
        if (bp[i].on && bp[i].addr == a) return 1;
    return 0;
}

static int gdb_trap(struct trapframe *tf)
{
    if (tf->vec == 3 && is_bp(tf->eip - 1))
        tf->eip--;                              /* report the breakpoint's own address */
    tf->eflags &= ~EF_TF;
    if (resumed)
        send_packet("S05");                     /* the first stop waits for gdb's '?' */
    for (;;) {
        u32 a, n, i;
        const char *p;
        recv_packet();
        switch (pkt[0]) {
        case '?': send_packet("S05"); break;
        case 'g': regs_out(tf); send_packet(out); break;
        case 'G': regs_in(tf, pkt + 1); send_packet("OK"); break;
        case 'm':
            p = get_hex(pkt + 1, &a);
            get_hex(p + 1, &n);
            for (i = 0; i < n && i * 2 + 2 < sizeof out; i++) {
                u8 b;
                if (read_byte(a + i, &b)) break;
                out[i * 2] = hexd[b >> 4];
                out[i * 2 + 1] = hexd[b & 15];
            }
            out[i * 2] = 0;
            send_packet(i ? out : "E01");
            break;
        case 'M':
            p = get_hex(pkt + 1, &a);
            p = get_hex(p + 1, &n) + 1;
            for (i = 0; i < n; i++, p += 2)
                if (try_wr8(a + i, (u32)((hexv(p[0]) << 4) | hexv(p[1])))) break;
            send_packet(i == n ? "OK" : "E01");
            break;
        case 'Z': case 'z':
            if (pkt[1] != '0') { send_packet(""); break; }
            get_hex(pkt + 3, &a);
            send_packet(set_bp(a, pkt[0] == 'Z') ? "E01" : "OK");
            break;
        case 's':
            if (pkt[1]) get_hex(pkt + 1, &tf->eip);
            tf->eflags |= EF_TF;
            resumed = 1;
            return 1;
        case 'c':
            if (pkt[1]) get_hex(pkt + 1, &tf->eip);
            resumed = 1;
            return 1;
        case 'D': send_packet("OK"); return 1;
        case 'k': return 1;
        case 'H': send_packet("OK"); break;
        case 'q':
            if (!memcmp(pkt, "qSupported", 10)) send_packet("PacketSize=1f0");
            else if (!memcmp(pkt, "qAttached", 9)) send_packet("1");
            else send_packet("");
            break;
        default: send_packet(""); break;
        }
    }
}

void gdb_init(u16 p)
{
    port = p;
    serial_init(port);
    while (serial_getc(port) >= 0) ;
    set_trap_handler(1, gdb_trap);
    set_trap_handler(3, gdb_trap);
}
