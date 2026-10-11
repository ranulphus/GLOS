/* The SSH server in the kernel (PRD §7; M3 item 6). lwIP's TCP (port 22)
 * lives in the net thread, the protocol (ssh.c) in the ssh thread, of the
 * bulk class: a key exchange takes hundreds of milliseconds on a 486 and
 * must not hold the network up. Between them, per connection, two rings,
 * each with one writer and one reader, so neither side waits on a lock:
 *   rx  bytes from the client: the net thread writes, the ssh thread reads;
 *   tx  bytes to the client: the other way round.
 * The host key and AUTHKEYS come from KEYS\ beside GLOS.EXE (bootinfo);
 * without a host key there is no SSH (GLOS-SSH off). A command is a
 * built-in "glos ..." one, or, headless, a DOS command line for the agent
 * (kernel/dos/agent.c), whose output and exit code come back here. */
#include <string.h>

#include "lwip/tcp.h"

#include "glos/bootinfo.h"
#include "agent.h"
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "dos.h"
#include "dpmi.h"
#include "gdbprog.h"
#include "nic.h"
#include "sched.h"
#include "session.h"
#include "sftp.h"
#include "shot.h"
#include "ssh.h"
#include "sshbuf.h"
#include "sshd.h"
#include "timer.h"
#include "vm.h"

#define NCONN      4
#define RING       0x10000u                     /* a power of two */
#define GRACE      (60 * 1024)                  /* ticks to log in */

struct ring {
    u8 *buf;
    volatile u32 head, tail;                    /* head: next write; tail: next read */
};

struct sconn {
    u8 used;
    volatile u8 net_closed;                     /* net -> ssh: the peer or the stack ended it */
    volatile u8 ssh_done;                       /* ssh -> net: close once tx is empty */
    struct tcp_pcb *pcb;                        /* the net thread's */
    struct ssh_conn *conn;                      /* the ssh thread's */
    struct ring rx, tx;
    u32 since;
};

static struct sconn conns[NCONN];
static struct ssh_server srv;
static struct waitq sshq;
static struct thread *ssh_thread;
static volatile u8 ssh_kick;
static void (*kick_net)(void);
static int agent_mode;
static struct sftp *sftps[8];                   /* SFTP sessions, kept until they have closed their files */

/* ---- rings: one writer, one reader; x86 keeps stores in order */

static u32 ring_used(const struct ring *r) { return r->head - r->tail; }

static u32 ring_put(struct ring *r, const u8 *p, u32 n)
{
    u32 room = RING - ring_used(r), i;
    if (n > room)
        n = room;
    for (i = 0; i < n; i++)
        r->buf[(r->head + i) & (RING - 1)] = p[i];
    __asm__ volatile("" ::: "memory");
    r->head += n;
    return n;
}

static u32 ring_get(struct ring *r, u8 *p, u32 max)
{
    u32 n = ring_used(r), i;
    if (n > max)
        n = max;
    for (i = 0; i < n; i++)
        p[i] = r->buf[(r->tail + i) & (RING - 1)];
    __asm__ volatile("" ::: "memory");
    r->tail += n;
    return n;
}

/* From the net thread's lwIP callbacks, which run with interrupts on. */
static void wake_ssh(void)
{
    u32 f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory");
    ssh_kick = 1;
    thread_wake(&sshq);
    if (f & 0x200)
        sti();
}

/* ---- the net thread's side */

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct sconn *s = arg;
    struct pbuf *q;
    (void)err;
    if (!p) {
        s->net_closed = 1;
        wake_ssh();
        return ERR_OK;
    }
    if (RING - ring_used(&s->rx) < p->tot_len)
        return ERR_MEM;                         /* lwIP offers it again later */
    for (q = p; q; q = q->next)
        ring_put(&s->rx, q->payload, q->len);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    wake_ssh();
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)arg;
    (void)pcb;
    (void)len;
    wake_ssh();                                 /* room for more output */
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    struct sconn *s = arg;
    (void)err;
    s->pcb = NULL;                              /* lwIP has freed it */
    s->net_closed = 1;
    wake_ssh();
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    int i;
    (void)arg;
    if (err != ERR_OK || !pcb)
        return ERR_VAL;
    for (i = 0; i < NCONN && conns[i].used; i++) ;
    if (i == NCONN) {
        kprintf("GLOS-SSH refuse reason=busy\n");
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    conns[i].used = 1;
    conns[i].net_closed = conns[i].ssh_done = 0;
    conns[i].rx.head = conns[i].rx.tail = conns[i].tx.head = conns[i].tx.tail = 0;
    conns[i].pcb = pcb;
    conns[i].since = timer_ticks();
    tcp_arg(pcb, &conns[i]);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    tcp_nagle_disable(pcb);
    wake_ssh();
    return ERR_OK;
}

void sshd_net_start(void (*kick)(void))
{
    struct tcp_pcb *pcb;
    kick_net = kick;
    if (!ssh_thread)
        return;
    pcb = tcp_new();
    if (!pcb || tcp_bind(pcb, IP_ADDR_ANY, 22) != ERR_OK || !(pcb = tcp_listen(pcb))) {
        kprintf("GLOS-SSH error=listen\n");
        return;
    }
    tcp_accept(pcb, on_accept);
    kprintf("GLOS-SSH listen port=22 authkeys=%u\n", srv.n_authkeys);
}

/* Each turn of the net thread: output into TCP, and the closes. */
void sshd_net_poll(void)
{
    int i;
    for (i = 0; i < NCONN; i++) {
        struct sconn *s = &conns[i];
        u8 chunk[1460];
        if (!s->used)
            continue;
        while (s->pcb && ring_used(&s->tx) && tcp_sndbuf(s->pcb) > 0) {
            u32 k = ring_used(&s->tx), room = tcp_sndbuf(s->pcb);
            if (k > sizeof chunk) k = sizeof chunk;
            if (k > room) k = room;
            k = ring_get(&s->tx, chunk, k);
            if (tcp_write(s->pcb, chunk, (u16_t)k, TCP_WRITE_FLAG_COPY) != ERR_OK)
                break;
        }
        if (s->pcb)
            tcp_output(s->pcb);
        if (s->ssh_done && !ring_used(&s->tx)) {    /* the ssh thread has let go */
            if (s->pcb) {
                tcp_arg(s->pcb, NULL);
                tcp_recv(s->pcb, NULL);
                tcp_sent(s->pcb, NULL);
                tcp_err(s->pcb, NULL);
                if (tcp_close(s->pcb) != ERR_OK)
                    tcp_abort(s->pcb);
                s->pcb = NULL;
            }
            s->used = 0;
        }
    }
}

/* ---- the ssh thread's side */

static void *k_alloc(size_t n)
{
    void *p = kmalloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

static void k_free(void *p)
{
    if (p)
        kfree(p);
}

static void k_send(void *io, const uint8_t *d, uint32_t n)
{
    struct sconn *s = io;
    while (n && !s->net_closed) {
        u32 k = ring_put(&s->tx, d, n);
        d += k;
        n -= k;
        kick_net();
        if (n)                                  /* full: wait for the net thread to drain it */
            thread_wait(&sshq, timer_ticks() + 64);
    }
}

static void k_log(const char *fmt, ...)
{
    char line[200];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(line, sizeof line, fmt, ap);
    __builtin_va_end(ap);
    kprintf("GLOS-SSH %s\n", line);
}

static void say(struct ssh_chan *ch, int stream, const char *s)
{
    ssh_chan_write(ch, stream, (const uint8_t *)s, (uint32_t)strlen(s));
}

/* A built-in that takes time, or has more to say than a channel buffers: its
   turn() is called on every turn of this thread, writes what the channel
   has room for, and returns 1 once it has ended the channel. */
#define ROOM 0x8000u                            /* output a channel may have waiting */
struct stream {
    struct ssh_chan *ch;                        /* 0: free */
    int (*turn)(struct ssh_chan *ch, void *arg);
    void (*drop)(void *arg);
    void *arg;
};
static struct stream streams[8];
static u32 stream_due;                          /* the tick a stream next wants a turn at whatever happens; 0: none */

static void stream_at(u32 tick)
{
    if (!tick)
        tick = 1;
    if (!stream_due || (s32)(tick - stream_due) < 0)
        stream_due = tick;
}

static int stream_add(struct ssh_chan *ch, int (*turn)(struct ssh_chan *, void *), void (*drop)(void *), void *arg)
{
    u32 i;
    for (i = 0; i < ARRAY_SIZE(streams) && streams[i].ch; i++) ;
    if (i == ARRAY_SIZE(streams))
        return -1;
    streams[i].turn = turn;
    streams[i].drop = drop;
    streams[i].arg = arg;
    streams[i].ch = ch;
    return 0;
}

static void streams_turn(void)
{
    u32 i;
    stream_due = 0;
    for (i = 0; i < ARRAY_SIZE(streams); i++)
        if (streams[i].ch && streams[i].turn(streams[i].ch, streams[i].arg)) {
            streams[i].drop(streams[i].arg);
            streams[i].ch = 0;
        }
}

static void streams_gone(struct ssh_chan *ch)
{
    u32 i;
    for (i = 0; i < ARRAY_SIZE(streams); i++)
        if (streams[i].ch == ch) {
            streams[i].drop(streams[i].arg);
            streams[i].ch = 0;
        }
}

/* glos shot: the capture is the VM thread's (kernel/dos/shot.c); then the
   file, a piece at a time. */
static int shot_turn(struct ssh_chan *ch, void *arg)
{
    static u8 piece[SHOT_PIECE];
    struct shot *sh = arg;
    int st = shot_poll(sh);
    u32 n;
    if (!st)
        return 0;
    if (st < 0) {
        say(ch, 1, "glos shot: ");
        say(ch, 1, shot_why(sh));
        say(ch, 1, "\n");
        ssh_chan_exit(ch, 1);
        return 1;
    }
    while (ssh_chan_pending(ch) < ROOM) {
        if (!(n = shot_read(sh, piece))) {
            ssh_chan_exit(ch, 0);
            return 1;
        }
        ssh_chan_write(ch, 0, piece, n);
    }
    return 0;
}

static void shot_drop(void *arg) { shot_free(arg); }

/* glos key, type and mouse (PRD §7.6): input into the program in front, as
   from the keyboard and the mouse (kernel/vm/vkbc.c), each event at its
   time from the command's start. */
enum { EV_DOWN, EV_UP, EV_MOUSE };
struct inev {
    u32 at;                                     /* ticks */
    u8 kind;
    int a, b, c;                                /* the key's code; the mouse's dx, dy, buttons (-1: as they are) */
};
struct input {
    u32 t0, n, i, max;
    struct inev ev[1];
};
#define TAP 51                                  /* ticks a tapped key stays down: 50 ms, as 86Box's monitor */

static struct input *input_new(u32 max)
{
    struct input *in = kmalloc(sizeof *in + max * sizeof in->ev[0]);
    if (in) {
        in->n = in->i = 0;
        in->max = max;
    }
    return in;
}

/* An event into its place by time (those at the same time stay in the order given). */
static void input_add(struct input *in, u32 at, int kind, int a, int b, int c)
{
    u32 i;
    if (in->n >= in->max)
        return;
    for (i = in->n; i > 0 && in->ev[i - 1].at > at; i--)
        in->ev[i] = in->ev[i - 1];
    in->ev[i].at = at;
    in->ev[i].kind = (u8)kind;
    in->ev[i].a = a;
    in->ev[i].b = b;
    in->ev[i].c = c;
    in->n++;
}

static int input_turn(struct ssh_chan *ch, void *arg)
{
    struct input *in = arg;
    u32 now = timer_ticks();
    while (in->i < in->n && (s32)(now - (in->t0 + in->ev[in->i].at)) >= 0) {
        const struct inev *e = &in->ev[in->i];
        int r = e->kind == EV_MOUSE ? vkbc_mouse(e->a, e->b, e->c) : vkbc_key((u32)e->a, e->kind == EV_DOWN);
        if (r == -1) {                          /* the queue is full: when the program has read some */
            stream_at(now + 16);
            return 0;
        }
        if (r == -2) {
            say(ch, 1, "glos mouse: no program has the mouse on\n");
            ssh_chan_exit(ch, 1);
            return 1;
        }
        in->i++;
    }
    if (in->i == in->n) {
        ssh_chan_exit(ch, 0);
        return 1;
    }
    stream_at(in->t0 + in->ev[in->i].at);
    return 0;
}

static void input_drop(void *arg) { kfree(arg); }

/* A number (decimal, 0x hexadecimal, a minus sign) at *p, which moves past it: 0, or -1 if there is none. */
static int number(const char **p, int *v)
{
    const char *s = *p;
    int neg = *s == '-', n = 0, digits = 0;
    if (neg)
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (s += 2;; s++, digits++) {
            int d = *s >= '0' && *s <= '9' ? *s - '0' : (*s | 32) >= 'a' && (*s | 32) <= 'f' ? (*s | 32) - 'a' + 10 : -1;
            if (d < 0)
                break;
            n = n * 16 + d;
        }
    } else {
        for (; *s >= '0' && *s <= '9'; s++, digits++)
            n = n * 10 + (*s - '0');
    }
    if (!digits)
        return -1;
    *v = neg ? -n : n;
    *p = s;
    return 0;
}

/* Seconds with a fraction ("1.25") as ticks. */
static int seconds(const char **p, u32 *ticks)
{
    const char *s = *p;
    u32 whole = 0, frac = 0, scale = 1, digits = 0;
    for (; *s >= '0' && *s <= '9'; s++, digits++)
        whole = whole * 10 + (u32)(*s - '0');
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++, digits++)
            if (scale < 1000) {
                frac = frac * 10 + (u32)(*s - '0');
                scale *= 10;
            }
    if (!digits || whole > 3600)
        return -1;
    *ticks = whole * 1024 + frac * 1024 / scale;
    *p = s;
    return 0;
}

/* glos key's list, Loop A's --keys (MGA-Glide's tools/loopa/run.py): items
   SECONDS:CODE[:down|up] and SECONDS:mouse:DX:DY[:BUTTONS] with commas
   between; the SECONDS: may be left out (now). A code is the key's set 1
   make code, 100h and above for the keys with an E0 prefix; without down
   or up it is a tap. 0, or the item that makes no sense. */
static const char *input_parse(struct input *in, const char *p)
{
    while (*p) {
        const char *item = p, *q;
        u32 at = 0;
        int code, dx, dy, buttons = -1;
        for (q = p; *q && *q != ',' && *q != ':'; q++) ;
        if (*q == ':' && strncmp(p, "mouse", 5) && (q[1] < 'a' || q[1] == 'm')) {   /* SECONDS: first */
            if (seconds(&p, &at) != 0 || *p++ != ':')
                return item;
        }
        if (!strncmp(p, "mouse:", 6)) {
            p += 6;
            if (number(&p, &dx) != 0 || *p++ != ':' || number(&p, &dy) != 0)
                return item;
            if (*p == ':' && (p++, number(&p, &buttons) != 0))
                return item;
            input_add(in, at, EV_MOUSE, dx, dy, buttons);
        } else {
            if (number(&p, &code) != 0 || code < 1 || code > 0x1FF || (code & 0x80))
                return item;
            if (!strncmp(p, ":down", 5)) {
                p += 5;
                input_add(in, at, EV_DOWN, code, 0, 0);
            } else if (!strncmp(p, ":up", 3)) {
                p += 3;
                input_add(in, at, EV_UP, code, 0, 0);
            } else {
                if (!strncmp(p, ":tap", 4))
                    p += 4;
                input_add(in, at, EV_DOWN, code, 0, 0);
                input_add(in, at + TAP, EV_UP, code, 0, 0);
            }
        }
        if (*p == ',')
            p++;
        else if (*p)
            return item;
    }
    return 0;
}

/* The key for a character on the US layout: its code, 200h added with Shift; 0 if it has none. */
static int us_key(char c)
{
    static const char plain[] = "\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shift[] = "\0!@#$%^&*()_+\0\0QWERTYUIOP{}\0\0ASDFGHJKL:\"~\0|ZXCVBNM<>?";
    u32 i;
    if (!c)
        return 0;
    for (i = 0; i < sizeof plain - 1; i++)
        if (plain[i] == c && i != 0x36 && i != 0x37)
            return (int)i + 1;
    for (i = 0; i < sizeof shift - 1; i++)
        if (shift[i] == c)
            return ((int)i + 1) | 0x200;
    return 0;
}

/* glos type's text: each character a key, 40 ticks apart; \n, \t, \e, \b and \\ as in C.
   0, or the character without a key. */
static const char *type_parse(struct input *in, const char *p)
{
    u32 at = 0;
    for (; *p; p++, at += 40) {
        char c = *p;
        int k;
        if (c == '\\' && p[1]) {
            p++;
            c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'e' ? 27 : *p == 'b' ? '\b' : *p;
        }
        if (!(k = us_key(c)))
            return p;
        if (k & 0x200)
            input_add(in, at, EV_DOWN, 0x2A, 0, 0);
        input_add(in, at + 5, EV_DOWN, k & 0xFF, 0, 0);
        input_add(in, at + 20, EV_UP, k & 0xFF, 0, 0);
        if (k & 0x200)
            input_add(in, at + 25, EV_UP, 0x2A, 0, 0);
    }
    return 0;
}

/* glos key LIST, glos type TEXT, glos mouse DX DY [BUTTONS]. */
static void input_cmd(struct ssh_chan *ch, const char *cmd)
{
    const char *arg = cmd + (cmd[5] == 'k' ? 9 : cmd[5] == 't' ? 10 : 11), *bad = 0;
    const char *what = cmd[5] == 'k' ? "key" : cmd[5] == 't' ? "type" : "mouse";
    struct input *in = strlen(arg) > 2000 ? 0 : input_new(cmd[5] == 't' ? (u32)strlen(arg) * 4 + 1 : (u32)strlen(arg) + 2);
    int dx, dy, buttons = -1;
    if (!in) {
        say(ch, 1, "glos: no memory\n");
        ssh_chan_exit(ch, 1);
        return;
    }
    if (cmd[5] == 'k') {
        bad = input_parse(in, arg);
    } else if (cmd[5] == 't') {
        bad = type_parse(in, arg);
    } else {
        const char *p = arg;
        if (number(&p, &dx) != 0 || *p++ != ' ' || number(&p, &dy) != 0 || (*p == ' ' && (p++, number(&p, &buttons) != 0))
            || *p)
            bad = arg;
        else
            input_add(in, 0, EV_MOUSE, dx, dy, buttons);
    }
    if (bad || !in->n) {
        say(ch, 1, "glos ");
        say(ch, 1, what);
        say(ch, 1, bad ? ": not understood: " : ": nothing to do");
        say(ch, 1, bad ? bad : "");
        say(ch, 1, "\n");
        ssh_chan_exit(ch, 2);
        kfree(in);
        return;
    }
    in->t0 = timer_ticks();
    if (stream_add(ch, input_turn, input_drop, in) != 0) {
        say(ch, 1, "glos: busy\n");
        ssh_chan_exit(ch, 1);
        kfree(in);
    }
}

/* glos log --follow: the COM1 mirror from its last 16 KB on, until the
   client goes. The log is looked at sixteen times a second. */
static int follow_turn(struct ssh_chan *ch, void *arg)
{
    static char buf[2048];
    u32 *pos = arg, n;
    while (ssh_chan_pending(ch) < ROOM && (n = klog_from(pos, buf, sizeof buf)) != 0)
        ssh_chan_write(ch, 0, (const uint8_t *)buf, n);
    stream_at(timer_ticks() + 64);
    return 0;
}

static void plain_drop(void *arg) { kfree(arg); }

/* glos stat: one line of JSON. Counters count from GLOS's start; ticks are
   1024 to the second. */
static u32 stat_line(char *b, u32 max)
{
    u32 n = 0, i;
    n += (u32)ksnprintf(b + n, max - n, "{\"ticks\":%u,\"hz\":1024,\"threads\":{", timer_ticks());
    n += sched_stat(b + n, max - n);
    n += (u32)ksnprintf(b + n, max - n, "},\"mem\":{\"total_kb\":%u,\"free_kb\":%u,\"heap_kb\":%u},\"irq\":[",
                        pmm_total_frames() * 4, pmm_free_frames() * 4, kheap_bytes() / 1024);
    for (i = 0; i < 16; i++)
        n += (u32)ksnprintf(b + n, max - n, "%s%u", i ? "," : "", cpu_irq_count((int)i));
    n += (u32)ksnprintf(b + n, max - n, "],\"vm\":{\"gp\":%u,\"int\":%u,\"irq\":%u},\"net\":", vm.n_gp, vm.n_int, vm.n_irq);
    n += net_stat(b + n, max - n, 1);
    n += (u32)ksnprintf(b + n, max - n, ",\"session\":");
    n += session_stat(b + n, max - n);
    n += (u32)ksnprintf(b + n, max - n, ",\"dpmi\":");
    n += dpmi_stat(b + n, max - n);
    n += (u32)ksnprintf(b + n, max - n, "}\n");
    return n < max ? n : max;
}

/* glos stat --follow: a line a second. */
static int stat_turn(struct ssh_chan *ch, void *arg)
{
    static char line[1536];
    u32 *next = arg, now = timer_ticks();
    if ((s32)(now - *next) >= 0 && ssh_chan_pending(ch) < ROOM) {
        ssh_chan_write(ch, 0, (const uint8_t *)line, stat_line(line, sizeof line));
        *next = now + 1024;
    }
    stream_at(*next);
    return 0;
}

static u32 reboot_at;                           /* glos reboot: the tick to reset at; 0: no */

/* glos gdb (kernel/dbg/gdbprog.c): gdb's channel, a direct-tcpip one to GDBP_PORT. */
static struct ssh_chan *gdb_chan;

static void gdb_send(const u8 *d, u32 n)
{
    if (gdb_chan)
        ssh_chan_write(gdb_chan, 0, d, n);
}

/* The built-in commands, and DOS command lines for the agent. */
static int k_exec(struct ssh_chan *ch, const char *cmd)
{
    static const char ver[] = "GLOS M3 (" __DATE__ ")\n";
    kprintf("GLOS-SSH exec=\"%s\"\n", cmd);
    if (strncmp(cmd, "glos ", 5) && strcmp(cmd, "glos")) {
        if (!agent_mode) {
            say(ch, 1, "glos: DOS commands need GLOS started without /RUN or /SHELL\n");
            ssh_chan_exit(ch, 126);
        } else if (agent_post(ch, cmd) != 0) {
            say(ch, 1, "glos: busy\n");
            ssh_chan_exit(ch, 126);
        }
        return 0;
    }
    if (!strncmp(cmd, "glos run", 8) && (cmd[8] == ' ' || !cmd[8])) {
        const char *p = cmd + 8;                /* glos run [--exclusive|--app] [--direct] [--profile NAME] COMMAND */
        char prof[16];
        int direct = 0;
        u32 n;
        prof[0] = 0;
        for (;;) {
            while (*p == ' ')
                p++;
            if (!strncmp(p, "--exclusive", 11) && (p[11] == ' ' || !p[11])) {
                p += 11;                        /* (every session is, until M6's apps) */
            } else if (!strncmp(p, "--direct", 8) && (p[8] == ' ' || !p[8])) {
                direct = 1;
                p += 8;
            } else if (!strncmp(p, "--app", 5) && (p[5] == ' ' || !p[5])) {
                say(ch, 1, "glos run: --app needs GLOS apps (M6)\n");
                ssh_chan_exit(ch, 2);
                return 0;
            } else if (!strncmp(p, "--profile ", 10)) {
                for (p += 10; *p == ' '; p++) ;
                for (n = 0; p[n] && p[n] != ' ' && n < sizeof prof - 1; n++)
                    prof[n] = p[n];
                prof[n] = 0;
                p += n;
                if (!session_profile_known(prof)) {
                    say(ch, 1, "glos run: no [program ");
                    say(ch, 1, prof);
                    say(ch, 1, "] in GLOS.CFG\n");
                    ssh_chan_exit(ch, 2);
                    return 0;
                }
            } else {
                break;
            }
        }
        if (!*p || *p == '-') {
            say(ch, 1, "glos run: usage: glos run [--exclusive|--app] [--direct] [--profile NAME] COMMAND\n");
            ssh_chan_exit(ch, 2);
        } else if (!agent_mode) {
            say(ch, 1, "glos: DOS commands need GLOS started without /RUN or /SHELL\n");
            ssh_chan_exit(ch, 126);
        } else if (agent_post_run(ch, p, direct, prof) != 0) {
            say(ch, 1, "glos: busy\n");
            ssh_chan_exit(ch, 126);
        }
        return 0;
    }
    if (!strcmp(cmd, "glos tick")) {              /* the kernel's tick count: the gate's probe (M4e) */
        char t[32];
        ksnprintf(t, sizeof t, "ticks=%u hz=1024\n", timer_ticks());
        say(ch, 0, t);
        ssh_chan_exit(ch, 0);
        return 0;
    }
    if (!strcmp(cmd, "glos ver")) {
        say(ch, 0, ver);
        ssh_chan_exit(ch, 0);
    } else if (!strncmp(cmd, "glos echo ", 10)) {
        say(ch, 0, cmd + 10);
        say(ch, 0, "\n");
        ssh_chan_exit(ch, 0);
    } else if (!strcmp(cmd, "glos shot") || !strcmp(cmd, "glos shot --raw")) {
        struct shot *sh = shot_new(cmd[9] != 0, wake_ssh);
        if (!sh || stream_add(ch, shot_turn, shot_drop, sh) != 0) {
            if (sh)
                shot_free(sh);
            say(ch, 1, sh ? "glos shot: busy\n" : "glos shot: no memory\n");
            ssh_chan_exit(ch, 1);
        }
    } else if (!strncmp(cmd, "glos key ", 9) || !strncmp(cmd, "glos type ", 10) || !strncmp(cmd, "glos mouse ", 11)) {
        input_cmd(ch, cmd);
    } else if (!strcmp(cmd, "glos log --follow") || !strcmp(cmd, "glos stat --follow")) {
        u32 *st = kmalloc(sizeof *st);
        if (st)                                 /* the log's place, or the next line's time */
            *st = cmd[5] == 'l' ? (klog_pos() > 0x4000 ? klog_pos() - 0x4000 : 0) : timer_ticks();
        if (!st || stream_add(ch, cmd[5] == 'l' ? follow_turn : stat_turn, plain_drop, st) != 0) {
            if (st)
                kfree(st);
            say(ch, 1, "glos: busy\n");
            ssh_chan_exit(ch, 1);
        }
    } else if (!strcmp(cmd, "glos stat") || !strcmp(cmd, "glos net") || !strcmp(cmd, "glos mem")) {
        char *b = kmalloc(1536);
        u32 n = 0;
        if (!b) {
            say(ch, 1, "glos: no memory\n");
            ssh_chan_exit(ch, 1);
            return 0;
        }
        if (cmd[5] == 's') {
            n = stat_line(b, 1536);
        } else if (cmd[5] == 'n') {
            n = net_stat(b, 1536, 0);
        } else {                                /* glos mem: where the memory GLOS has is */
            n = (u32)ksnprintf(b, 1536, "memory total_kb=%u free_kb=%u\nkernel image_kb=%u heap_kb=%u heap_max_kb=%u\n",
                               pmm_total_frames() * 4, pmm_free_frames() * 4, kernel_bytes() / 1024, kheap_bytes() / 1024,
                               HEAP_MAX / 1024);
            n += (u32)ksnprintf(b + n, 1536 - n, "dpmi ");
            n += dpmi_stat(b + n, 1536 - n);
            n += (u32)ksnprintf(b + n, 1536 - n, "\n");
        }
        ssh_chan_write(ch, 0, (const uint8_t *)b, n);
        kfree(b);
        ssh_chan_exit(ch, 0);
    } else if (!strncmp(cmd, "glos kill ", 10)) {
        const char *q = cmd + 10;
        int seq;
        if (number(&q, &seq) != 0 || *q || agent_kill_seq((u32)seq) != 0) {
            say(ch, 1, "glos kill: no such job (glos ps lists them)\n");
            ssh_chan_exit(ch, 1);
        } else {
            ssh_chan_exit(ch, 0);
        }
    } else if (!strcmp(cmd, "glos gdb")) {
        char t[160];
        gdbp_arm(wake_ssh);
        ksnprintf(t, sizeof t, "glos gdb: the program stops when it next runs its 32-bit code, and waits for gdb\n"
                  "at port %u here: ssh -W localhost:%u, or ssh -L\n", GDBP_PORT, GDBP_PORT);
        say(ch, 0, t);
        ssh_chan_exit(ch, 0);
    } else if (!strcmp(cmd, "glos reboot") || !strcmp(cmd, "glos reboot --force")) {
        if (!cmd[11] && agent_busy()) {
            say(ch, 1, "glos reboot: a job is running or waiting (glos kill it, or glos reboot --force)\n");
            ssh_chan_exit(ch, 1);
        } else {
            ssh_chan_exit(ch, 0);
            reboot_at = (timer_ticks() + 512) | 1;      /* half a second for the reply to leave */
            stream_at(reboot_at);
        }
    } else if (!strcmp(cmd, "glos log") || !strcmp(cmd, "glos ps")) {
        char *buf = kmalloc(0x4000);
        u32 n = 0;
        u16 psp;
        if (!buf) {
            say(ch, 1, "glos: no memory\n");
            ssh_chan_exit(ch, 1);
            return 0;
        }
        if (cmd[5] == 'l') {
            n = klog_read(buf, 0x4000);
        } else {
            n = agent_ps(buf, 0x1000);
            psp = vm_current_psp();
            if (psp) {
                u32 mcb = ((u32)psp - 1) << 4, k;
                n += (u32)ksnprintf(buf + n, 0x2000 - n, "dos psp=%04x name=", psp);
                for (k = 0; k < 8 && vm_rd8(mcb + 8 + k) > ' '; k++)
                    buf[n++] = (char)vm_rd8(mcb + 8 + k);
                buf[n++] = '\n';
            }
            n += sched_ps(buf + n, 0x4000 - n);
        }
        ssh_chan_write(ch, 0, (const uint8_t *)buf, n);
        kfree(buf);
        ssh_chan_exit(ch, 0);
    } else if (!strcmp(cmd, "glos kill")) {
        if (agent_kill() != 0) {
            say(ch, 1, "glos kill: nothing is running\n");
            ssh_chan_exit(ch, 1);
        } else {
            ssh_chan_exit(ch, 0);
        }
    } else if (!strcmp(cmd, "glos exit")) {
        if (!agent_mode) {
            say(ch, 1, "glos: exit needs GLOS started without /RUN or /SHELL\n");
            ssh_chan_exit(ch, 1);
        } else {
            ssh_chan_exit(ch, 0);
            agent_exit(512);                    /* half a second for the reply to leave */
        }
    } else {
        say(ch, 1, "glos: no such command (glos ver, echo, run, shot, key, type, mouse, log, stat, ps, mem, net, tick, "
                   "kill, gdb, exit, reboot)\n");
        ssh_chan_exit(ch, 127);
    }
    return 0;
}

/* The agent's output into a channel, while the client keeps up. */
static u32 agent_write(void *owner, int stream, const u8 *d, u32 n)
{
    struct ssh_chan *ch = owner;
    if (ssh_chan_pending(ch) > 0x8000)
        return 0;
    return ssh_chan_write(ch, stream, d, n);
}

static void agent_done(void *owner, u32 code) { ssh_chan_exit(owner, code); }

/* SFTP (kernel/ssh/sftp.c) on the DOS server: headless only, for now. */
static int k_subsystem(struct ssh_chan *ch, const char *name)
{
    struct sftp *s;
    u32 i;
    for (i = 0; i < ARRAY_SIZE(sftps) && sftps[i]; i++) ;
    if (strcmp(name, "sftp") || !agent_mode || i == ARRAY_SIZE(sftps) || !(s = sftp_open(ch))) {
        kprintf("GLOS-SSH subsystem=%s refused\n", name);
        return -1;
    }
    kprintf("GLOS-SSH subsystem=sftp\n");
    sftps[i] = s;
    ssh_chan_set_user(ch, s);
    return 0;
}

/* A direct-tcpip channel: to glos gdb's port here, once it is armed, and nowhere else. */
static int k_tcpip(struct ssh_chan *ch, const char *host, uint32_t port)
{
    if (port != GDBP_PORT || gdb_chan || (strcmp(host, "localhost") && strcmp(host, "127.0.0.1"))
        || gdbp_attach(gdb_send) != 0) {
        kprintf("GLOS-SSH forward to=%s:%u refused\n", host, port);
        return -1;
    }
    gdb_chan = ch;
    kprintf("GLOS-SSH forward to=gdb\n");
    return 0;
}

static void k_data(struct ssh_chan *ch, const uint8_t *d, uint32_t n)
{
    if (ch == gdb_chan) {
        gdbp_input(d, n);
        return;
    }
    if (ssh_chan_user(ch))
        sftp_input(ssh_chan_user(ch), d, n);
}

static void k_eof(struct ssh_chan *ch)
{
    if (ch == gdb_chan) {                       /* gdb has gone: its side closes too, and the program runs on */
        gdbp_detach();
        ssh_chan_close(ch);
        return;
    }
    if (ssh_chan_user(ch))
        sftp_eof(ssh_chan_user(ch));
}

static void k_gone(struct ssh_chan *ch)
{
    if (ch == gdb_chan) {
        gdbp_detach();
        gdb_chan = 0;
    }
    agent_drop(ch);
    streams_gone(ch);
    if (ssh_chan_user(ch))
        sftp_close(ssh_chan_user(ch));
}

static void sftp_turn(void)
{
    u32 i;
    for (i = 0; i < ARRAY_SIZE(sftps); i++)
        if (sftps[i] && sftp_service(sftps[i]) < 0) {
            sftp_free(sftps[i]);
            sftps[i] = 0;
        }
}

static const struct ssh_ops ops = { k_alloc, k_free, k_send, k_log, k_exec, k_subsystem, k_data, k_eof, k_gone, k_tcpip };

static void finish(struct sconn *s, const char *why)
{
    ssh_conn_free(s->conn);
    s->conn = NULL;
    kprintf("GLOS-SSH close slot=%u why=%s\n", (u32)(s - conns), why);
    s->ssh_done = 1;
    kick_net();
}

static void ssh_main(void *arg)
{
    static u8 buf[4096];
    int i;
    (void)arg;
    for (;;) {
        u32 f;
        __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory");
        if (!ssh_kick) {
            u32 until = timer_ticks() + 1024;
            if (stream_due && (s32)(stream_due - until) < 0)
                until = stream_due;
            if ((s32)(until - timer_ticks()) > 0)
                thread_wait(&sshq, until);
        }
        ssh_kick = 0;
        if (f & 0x200)
            sti();
        for (i = 0; i < NCONN; i++) {
            struct sconn *s = &conns[i];
            u32 n;
            if (!s->used || s->ssh_done)
                continue;
            if (!s->conn) {
                s->conn = ssh_conn_new(&srv, s);
                if (!s->conn) {
                    finish(s, "no-memory");
                    continue;
                }
                kprintf("GLOS-SSH connect slot=%u\n", (u32)i);
            }
            while ((n = ring_get(&s->rx, buf, sizeof buf)) != 0)
                if (ssh_conn_input(s->conn, buf, n) != 0)
                    break;
            if (!s->conn)
                continue;
            if (s->net_closed)
                finish(s, "peer");
            else if (ssh_conn_input(s->conn, buf, 0) != 0)
                finish(s, "protocol");
            else if (!ssh_conn_authenticated(s->conn) && timer_ticks() - s->since > GRACE)
                finish(s, "login-grace");
        }
        agent_service(agent_write, agent_done);
        sftp_turn();
        streams_turn();
        gdbp_turn();
        if (reboot_at) {
            if ((s32)(timer_ticks() - reboot_at) >= 0) {
                kprintf("GLOS-REBOOT\n");
                kprintf_flush(1);
                cpu_reboot();
            }
            stream_at(reboot_at);
        }
    }
}

void sshd_start(const struct bootinfo *bi)
{
    int i;
    srv.n_authkeys = ssh_parse_authkeys(bi->authkeys, bi->authkeys_len, srv.authkeys, SSH_MAX_AUTHKEYS);
    srv.ops = &ops;
    srv.version = "SSH-2.0-GLOS_M3";
    agent_mode = (bi->flags & BI_F_AGENT) != 0;
    agent_init(wake_ssh);
    dos_init(wake_ssh);
    if (!bi->hostkey_len || ssh_parse_privkey(bi->hostkey, bi->hostkey_len, srv.host_sk, srv.host_pk) != 0) {
        kprintf("GLOS-SSH off reason=%s\n", bi->hostkey_len ? "bad-hostkey" : "no-hostkey");
        return;
    }
    for (i = 0; i < NCONN; i++) {
        conns[i].rx.buf = kmalloc(RING);
        conns[i].tx.buf = kmalloc(RING);
        if (!conns[i].rx.buf || !conns[i].tx.buf)
            panic("sshd-rings", NULL);
    }
    ssh_thread = thread_create("ssh", PRIO_BULK, ssh_main, NULL);
}
