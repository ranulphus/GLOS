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
    } else if (!strcmp(cmd, "glos shot")) {
        const char *why = "";
        u32 len;
        u8 *png = shot_text(&len, &why);
        if (!png) {
            say(ch, 1, "glos shot: ");
            say(ch, 1, why);
            say(ch, 1, "\n");
            ssh_chan_exit(ch, 1);
        } else {
            ssh_chan_write(ch, 0, png, len);    /* fits: a stream buffers 256 KB */
            kfree(png);
            ssh_chan_exit(ch, 0);
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
        say(ch, 1, "glos: no such command (glos ver, echo, run, shot, log, ps, tick, kill, exit)\n");
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

static void k_data(struct ssh_chan *ch, const uint8_t *d, uint32_t n)
{
    if (ssh_chan_user(ch))
        sftp_input(ssh_chan_user(ch), d, n);
}

static void k_eof(struct ssh_chan *ch)
{
    if (ssh_chan_user(ch))
        sftp_eof(ssh_chan_user(ch));
}

static void k_gone(struct ssh_chan *ch)
{
    agent_drop(ch);
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

static const struct ssh_ops ops = { k_alloc, k_free, k_send, k_log, k_exec, k_subsystem, k_data, k_eof, k_gone };

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
        if (!ssh_kick)
            thread_wait(&sshq, timer_ticks() + 1024);
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
