/* The secrets store (API 2.0's SECRETS; docs/v1/phase-b.md step 13): apps' secrets by name, in a file of their
 * own beside GLOS.CFG (SECRETS.DAT), apart from the settings and from anything a user would share.
 *
 * The file is always encrypted and authenticated (secstore.c: ChaCha20 and Poly1305). What differs is the key:
 * - By default it is the PC's own: 32 random bytes in KEYS\SECRETS.KEY, made when the first secret is kept. That
 *   scrambles the file, no more: whoever has the disk has the key. It keeps secrets out of what is copied and
 *   backed up as "the settings".
 * - With a master password, the key is PBKDF2-HMAC-SHA-256 of it (a salt of 16 random bytes; as many rounds as
 *   this PC does in about half a second when the password is set, 4096 at least; both in the file's header).
 *   The store is then locked at each start, until the password is given once (secrets_unlock: a dialog of the
 *   desktop's, or `glos secrets unlock`). The key is kept in memory while GLOS runs; the password is not.
 *
 * The file is read and written on the config thread (config.c), through the agent's DOS server: so there is a
 * store in agent mode only. A writing goes to SECRETS.NEW, which then takes the file's place (a start that finds
 * only SECRETS.NEW uses it). Every writing has a new nonce. */
#include <string.h>

#include "randombytes.h"

#include "dos.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "secrets.h"
#include "secstore.h"
#include "timer.h"

static struct {
    u8 state;                                   /* SEC_ */
    u8 can;                                     /* there is a DOS server to keep it with */
    volatile u8 dirty;
    u8 busy;                                    /* the lock: one at a time through the store and its buffers */
    u8 have_pc_key, pc_key_new;                 /* the PC's key is known; ... and not yet in its file */
    u8 password;                                /* the key is a master password's */
    struct waitq lq, q;
    char path[80], tmp[80], keys[80], key_path[80];
    struct ss_head head;
    u8 key[32], pc_key[32];
    struct secstore st;
    u8 file[SS_FILE_MAX];
    u32 file_len;
    u32 writes, errors;
} s;

static void lock(void)
{
    u32 f = irq_save();
    while (s.busy)
        thread_block(&s.lq);
    s.busy = 1;
    irq_restore(f);
}

static void unlock(void)
{
    u32 f = irq_save();
    s.busy = 0;
    thread_wake(&s.lq);
    irq_restore(f);
}

void secrets_init(const char *cfg_path)
{
    u32 i, n = 0;
    for (i = 0; cfg_path[i]; i++)
        if (cfg_path[i] == '\\')
            n = i + 1;
    if (!n || n + 20 > sizeof s.path)
        return;
    memcpy(s.path, cfg_path, n), memcpy(s.path + n, "SECRETS.DAT", 12);
    memcpy(s.tmp, cfg_path, n), memcpy(s.tmp + n, "SECRETS.NEW", 12);
    memcpy(s.keys, cfg_path, n), memcpy(s.keys + n, "KEYS", 5);
    memcpy(s.key_path, cfg_path, n), memcpy(s.key_path + n, "KEYS\\SECRETS.KEY", 17);
}

/* ---- the files (the config thread) */

static int dos_wait(struct dos_req *r)
{
    if (dos_post(r) != 0)
        return -1;
    while (r->state != DR_DONE)
        thread_wait(&s.q, timer_ticks() + 64);
    return r->error ? -1 : 0;
}

/* A whole file read: its length, or -1 (none, or longer than the room). */
static int file_read(const char *path, u8 *out, u32 room)
{
    struct dos_req r;
    u16 h;
    memset(&r, 0, sizeof r);
    r.n = 1;
    r.r[0].ax = 0x3D00, r.r[0].ds = DOS_XFER;
    r.in = (const u8 *)path, r.in_len = strlen(path) + 1;
    if (dos_wait(&r) != 0)
        return -1;
    h = r.r[0].ax;
    if (room > dos_xfer_size())
        room = dos_xfer_size();
    memset(&r, 0, sizeof r);
    r.n = 2;
    r.r[0].ax = 0x3F00, r.r[0].bx = h, r.r[0].cx = (u16)room, r.r[0].ds = DOS_XFER;
    r.r[1].ax = 0x3E00, r.r[1].bx = h;
    r.out = out, r.out_len = room;
    if (dos_wait(&r) != 0 && r.ran < 1)
        return -1;
    return r.r[0].ax;
}

static int file_write(const char *path, const u8 *data, u32 n)
{
    struct dos_req r;
    u16 h;
    memset(&r, 0, sizeof r);
    r.n = 1;
    r.r[0].ax = 0x3C00, r.r[0].ds = DOS_XFER;
    r.in = (const u8 *)path, r.in_len = strlen(path) + 1;
    if (dos_wait(&r) != 0)
        return -1;
    h = r.r[0].ax;
    memset(&r, 0, sizeof r);
    r.n = 2;
    r.r[0].ax = 0x4000, r.r[0].bx = h, r.r[0].cx = (u16)n, r.r[0].ds = DOS_XFER;
    r.r[1].ax = 0x3E00, r.r[1].bx = h;
    r.in = data, r.in_len = n;
    return dos_wait(&r) == 0 && r.r[0].ax == n ? 0 : -1;
}

static void file_call(u16 ax, const char *path)                 /* (its failure is nothing: no such file, a folder there) */
{
    struct dos_req r;
    memset(&r, 0, sizeof r);
    r.n = 1;
    r.r[0].ax = ax, r.r[0].ds = DOS_XFER;
    r.in = (const u8 *)path, r.in_len = strlen(path) + 1;
    dos_wait(&r);
}

static int file_rename(const char *from, const char *to)
{
    struct dos_req r;
    char both[160];
    u32 a = strlen(from) + 1, b = strlen(to) + 1;
    memcpy(both, from, a);
    memcpy(both + a, to, b);
    memset(&r, 0, sizeof r);
    r.n = 1;
    r.r[0].ax = 0x5600, r.r[0].ds = DOS_XFER, r.r[0].dx = 0, r.r[0].es = DOS_XFER, r.r[0].di = (u16)a;
    r.in = (const u8 *)both, r.in_len = a + b;
    return dos_wait(&r);
}

void secrets_load(void)
{
    int n;
    if (!s.path[0])
        return;
    lock();
    s.can = 1;
    s.state = SEC_OPEN;                         /* (no file: an empty store, written when it has something) */
    if (file_read(s.key_path, s.pc_key, 33) == 32)
        s.have_pc_key = 1;
    n = file_read(s.path, s.file, sizeof s.file);
    if (n < 0)
        n = file_read(s.tmp, s.file, sizeof s.file);    /* (a writing that didn't get as far as the name) */
    if (n >= 0) {
        s.file_len = (u32)n;
        if (ss_head(s.file, s.file_len, &s.head) != 0) {
            s.state = SEC_BAD;
        } else if (s.head.flags & SS_F_PASSWORD) {
            s.password = 1;
            s.state = SEC_LOCKED;
        } else if (!s.have_pc_key || ss_open(s.file, s.file_len, s.pc_key, &s.st) != 0) {
            s.state = SEC_BAD;                  /* (the key's file is gone, or isn't this file's) */
        } else {
            memcpy(s.key, s.pc_key, 32);
        }
        kprintf("GLOS-SECRETS %s secrets=%u bytes=%d\n", s.state == SEC_OPEN ? "loaded" : s.state == SEC_LOCKED ? "locked"
                : "error=file", ss_count(&s.st, SS_SECRET), n);
    }
    unlock();
}

int secrets_dirty(void) { return s.can && s.dirty && s.state == SEC_OPEN; }

int secrets_save(void)
{
    u8 *out;
    u32 n;
    int ok;
    if (!(out = kmalloc(SS_FILE_MAX)))
        return -1;
    lock();
    s.dirty = 0;
    if (!s.password && !s.have_pc_key) {        /* the PC's key, at its first use */
        randombytes(s.pc_key, 32);
        s.have_pc_key = s.pc_key_new = 1;
        memcpy(s.key, s.pc_key, 32);
    }
    if (s.pc_key_new) {
        file_call(0x3900, s.keys);              /* (the folder, if it isn't there) */
        if (file_write(s.key_path, s.pc_key, 32) == 0)
            s.pc_key_new = 0;
    }
    s.head.flags = s.password ? SS_F_PASSWORD : 0;
    randombytes(s.head.nonce, 8);
    n = ss_seal(&s.st, &s.head, s.key, out);
    unlock();
    ok = !s.pc_key_new && file_write(s.tmp, out, n) == 0 ? 0 : -1;
    if (ok == 0) {
        file_call(0x4100, s.path);
        ok = file_rename(s.tmp, s.path);
    }
    kfree(out);
    if (ok == 0)
        s.writes++;
    else
        s.errors++;
    kprintf(ok == 0 ? "GLOS-SECRETS written secrets=%u\n" : "GLOS-SECRETS error=write secrets=%u\n", ss_count(&s.st, SS_SECRET));
    return ok;
}

/* ---- what is asked of it */

void kcfg_writer_kick(void);

static void changed(void)
{
    s.dirty = 1;
    kcfg_writer_kick();
}

static int usable(void) { return s.state == SEC_OPEN ? 0 : s.state == SEC_LOCKED ? SEC_E_LOCKED : s.state == SEC_BAD ? SEC_E_BAD : SEC_E_NONE; }

int secrets_state(void) { return s.state; }
int secrets_has_password(void) { return s.password; }

int secrets_put(const char *owner, const char *name, const u8 *value, u32 len)
{
    int r;
    lock();
    if ((r = usable()) == 0) {
        r = ss_put(&s.st, SS_SECRET, owner, name, value, len);
        r = r == 0 ? 0 : r == -1 ? SEC_E_ROOM : SEC_E_LONG;
        if (r == 0)
            changed();
    }
    unlock();
    return r;
}

int secrets_get(const char *owner, const char *name, u8 *out, u32 room, u32 *len)
{
    int r;
    lock();
    if ((r = usable()) == 0)
        r = ss_get(&s.st, SS_SECRET, owner, name, out, room, len) == 0 ? 0 : SEC_E_NONE;
    unlock();
    return r;
}

int secrets_remove(const char *owner, const char *name)
{
    int r;
    lock();
    if ((r = usable()) == 0) {
        r = ss_remove(&s.st, SS_SECRET, owner, name) == 0 ? 0 : SEC_E_NONE;
        if (r == 0)
            changed();
    }
    unlock();
    return r;
}

int secrets_grant(const char *prog, const char *name, int on)
{
    int r;
    lock();
    if ((r = usable()) == 0) {
        if (on)
            r = ss_put(&s.st, SS_GRANT, prog, name, 0, 0) == 0 ? 0 : SEC_E_ROOM;
        else
            r = ss_remove(&s.st, SS_GRANT, prog, name) == 0 ? 0 : SEC_E_NONE;
        if (r == 0)
            changed();
    }
    unlock();
    return r;
}

int secrets_granted(const char *prog, const char *name)
{
    int r;
    lock();
    r = usable() == 0 && ss_get(&s.st, SS_GRANT, prog, name, 0, 0, 0) == 0;
    unlock();
    return r;
}

int secrets_unlock(const char *password)
{
    u8 k[32];
    int r = SEC_E_NONE;
    lock();
    if (s.state == SEC_LOCKED) {
        pbkdf2_sha256((const u8 *)password, strlen(password), s.head.salt, 16, s.head.iters, k);
        if (ss_open(s.file, s.file_len, k, &s.st) == 0) {
            memcpy(s.key, k, 32);
            s.state = SEC_OPEN;
            r = 0;
        } else {
            r = SEC_E_WRONG;
        }
        memset(k, 0, sizeof k);
        kprintf("GLOS-SECRETS unlock %s\n", r == 0 ? "ok" : "wrong");
    }
    unlock();
    return r;
}

/* How many rounds this PC does in about half a second: measured, 4096 at least. */
static u32 rounds(void)
{
    u8 k[32];
    u32 n = 256, t0, dt;
    for (;;) {
        t0 = timer_ticks();
        pbkdf2_sha256((const u8 *)"calibrate", 9, (const u8 *)"0123456789abcdef", 16, n, k);
        dt = timer_ticks() - t0;
        if (dt >= 16 || n >= (1u << 20))
            break;
        n *= 4;
    }
    n = dt ? n * 512u / dt : n * 32;            /* (n is 2^20 at most: within 32 bits) */
    return n < 4096 ? 4096 : n > 4000000 ? 4000000 : n;
}

int secrets_password(const char *old, const char *now)
{
    u8 k[32];
    int r;
    lock();
    if ((r = usable()) == 0 && s.password) {    /* (changing it: the one it has first) */
        pbkdf2_sha256((const u8 *)old, strlen(old), s.head.salt, 16, s.head.iters, k);
        if (memcmp(k, s.key, 32) != 0)
            r = SEC_E_WRONG;
    }
    if (r == 0 && now[0]) {
        randombytes(s.head.salt, 16);
        s.head.iters = rounds();
        pbkdf2_sha256((const u8 *)now, strlen(now), s.head.salt, 16, s.head.iters, s.key);
        s.password = 1;
        changed();
        kprintf("GLOS-SECRETS password set rounds=%u\n", s.head.iters);
    } else if (r == 0) {
        s.password = 0;
        s.head.iters = 0;
        if (s.have_pc_key)
            memcpy(s.key, s.pc_key, 32);        /* (else made at the writing) */
        changed();
        kprintf("GLOS-SECRETS password removed\n");
    }
    memset(k, 0, sizeof k);
    unlock();
    return r;
}

u32 secrets_stat(char *buf, u32 max)
{
    static const char *const states[] = { "none", "open", "locked", "error" };
    return (u32)ksnprintf(buf, max, "secrets %s key=%s secrets=%u grants=%u bytes=%u writes=%u errors=%u\n", states[s.state],
                          s.password ? "password" : "pc", ss_count(&s.st, SS_SECRET), ss_count(&s.st, SS_GRANT), s.st.len,
                          s.writes, s.errors);
}
