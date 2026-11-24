/* GLOS's own DOS, its INT 21h (dos21.h). What isn't answered yet is said (DOS21_UNANSWERED: the caller logs it)
 * and refused as MS-DOS 7.1 refuses a function it hasn't got. Marked "to check": answers made from RBIL that
 * MS-DOS 7.1 itself is to be compared with (docs/v1/native-dos.md, N7). */
#include <string.h>

#include "dos21.h"
#include "dtime.h"
#include "exe.h"
#include "find.h"
#include "names.h"
#include "psp.h"
#include "glos/cp.h"

#define CF      0x0001u
#define ZF      0x0040u
const struct dos_layout dos_layout_machine = { 0x0540, 0x05A0, 0x05E0, 0x0800, 0x0118, 0x0128, 0xA000 };

/* ---- the machine's memory: nothing outside it is read (0) or written */

static unsigned long lin(unsigned seg, unsigned off) { return ((unsigned long)(seg & 0xFFFF) << 4) + (off & 0xFFFF); }
static unsigned rd8(struct dos_m *m, unsigned long a) { return a < m->size ? m->mem[a] : 0; }
static unsigned rd16(struct dos_m *m, unsigned long a) { return rd8(m, a) | rd8(m, a + 1) << 8; }

static void wr8(struct dos_m *m, unsigned long a, unsigned v)
{
    if (a < m->size)
        m->mem[a] = (unsigned char)v;
}

static void wr16(struct dos_m *m, unsigned long a, unsigned v)
{
    wr8(m, a, v);
    wr8(m, a + 1, v >> 8);
}

/* An ASCIZ string at seg:off into out (room bytes): 0, or -1 if it doesn't end there. */
static int str_in(struct dos_m *m, unsigned seg, unsigned off, char *out, unsigned room)
{
    unsigned i;

    for (i = 0; i < room; i++)
        if (!(out[i] = (char)rd8(m, lin(seg, off + i))))
            return 0;
    out[room - 1] = 0;
    return -1;
}

/* The SDA's current PSP and DTA. */
static unsigned cur_psp(struct dos_m *m) { return rd16(m, m->lay.sda + 0x10); }
static void set_psp(struct dos_m *m, unsigned seg) { wr16(m, m->lay.sda + 0x10, seg); }
static unsigned dta_seg(struct dos_m *m) { return rd16(m, m->lay.sda + 0x0E); }
static unsigned dta_off(struct dos_m *m) { return rd16(m, m->lay.sda + 0x0C); }

static void set_dta(struct dos_m *m, unsigned seg, unsigned off)
{
    wr16(m, m->lay.sda + 0x0C, off);
    wr16(m, m->lay.sda + 0x0E, seg);
}

unsigned dos_cur_psp(struct dos_m *m) { return cur_psp(m); }

static void str_out(struct dos_m *m, unsigned seg, unsigned off, const char *s)
{
    unsigned i;

    for (i = 0; s[i]; i++)
        wr8(m, lin(seg, off + i), (unsigned char)s[i]);
    wr8(m, lin(seg, off + i), 0);
}

/* ---- answers */

static unsigned ah_of(const struct dos_regs86 *r) { return (r->ax >> 8) & 0xFF; }
static unsigned al_of(const struct dos_regs86 *r) { return r->ax & 0xFF; }
static void set_al(struct dos_regs86 *r, unsigned v) { r->ax = (r->ax & 0xFF00) | (v & 0xFF); }

static int ok(struct dos_regs86 *r)
{
    r->flags &= ~CF;
    return DOS21_DONE;
}

static int err(struct dos_m *m, struct dos_regs86 *r, unsigned code)
{
    m->last_error = code;
    r->ax = code;
    r->flags |= CF;
    return DOS21_DONE;
}

static int done(struct dos_m *m, struct dos_regs86 *r, int e) { return e ? err(m, r, (unsigned)e) : ok(r); }

/* A volume's error as DOS gives it; nf: what "no such name" is for the call (02h a file, 03h a folder). */
static int fs_err(int r, int nf)
{
    switch (r) {
    case 0: return 0;
    case FAT_ENOENT: return nf;
    case FAT_ENOTDIR: return 0x03;
    case FAT_EINVAL: return nf == 0x02 ? 0x02 : 0x03;
    case FSV_ESHARE: return 0x20;
    case FSV_ENFILE: return 0x04;
    case FAT_EIO: case FAT_ECORRUPT: return 0x1F;  /* (general failure) */
    default: return 0x05;               /* (exists, full, not empty, read-only: access denied, as DOS says) */
    }
}

/* ---- names and paths */

/* A device's name as the last part of a path, in any folder, with or without an extension: DOS_DEV_*, or 0. */
static int dev_name(const char *path)
{
    static const struct { const char *n; int dev; } devs[] = {
        { "CON", DOS_DEV_CON }, { "NUL", DOS_DEV_NUL }, { "AUX", DOS_DEV_AUX }, { "COM1", DOS_DEV_AUX },
        { "PRN", DOS_DEV_PRN }, { "LPT1", DOS_DEV_PRN },
    };
    const char *b = path, *p;
    char base[9];
    unsigned i, k;

    for (p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            b = p + 1;
    for (k = 0; b[k] && b[k] != '.' && k < 8; k++)
        base[k] = (char)(b[k] >= 'a' && b[k] <= 'z' ? b[k] - 32 : b[k]);
    if (b[k] && b[k] != '.')
        return 0;
    base[k] = 0;
    for (i = 0; i < sizeof devs / sizeof devs[0]; i++)
        if (!strcmp(base, devs[i].n))
            return devs[i].dev;
    return 0;
}

/* 02h made 03h when it's a folder on the way that isn't there (as DOS says it), for path on v. */
static int no_file(struct fsv_vol *v, const char *path, int e)
{
    char dir[128];
    struct fat_ent fe;
    unsigned k, cut = 0;

    if (e != 0x02)
        return e;
    for (k = 0; path[k] && k < sizeof dir - 1; k++)
        if ((dir[k] = path[k]) == '\\')
            cut = k;
    if (cut == 0)
        return e;                               /* (in the root) */
    dir[cut] = 0;
    return fsv_lookup(v, dir, &fe) == 0 && (fe.attr & FAT_A_DIR) ? e : 0x03;
}

/* A path made a drive's volume and its whole path there (out: 128 bytes): 0, or a DOS error. */
static int resolve_str(struct dos_m *m, const char *p, struct fsv_vol **v, int *drive, char *out)
{
    int r = dos_resolve(&m->cds, m->cp, p, drive, out, 128);

    if (r)
        return r;
    if (!(*v = m->vol[*drive]))
        return 0x0F;
    return 0;
}

static int resolve(struct dos_m *m, unsigned seg, unsigned off, struct fsv_vol **v, int *drive, char *out)
{
    char p[128];

    if (str_in(m, seg, off, p, sizeof p) != 0)
        return 0x03;
    return resolve_str(m, p, v, drive, out);
}

/* ---- the console */

static void con_out(struct dos_m *m, const char *s, unsigned n)
{
    if (m->out && n)
        m->out(m->ctx, s, n);
}

/* The PC's scan code for a character as a US keyboard types it (0: none). */
static unsigned scan_for(unsigned char c)
{
    static const char *const rows[] = { "1234567890-=", "!@#$%^&*()_+", "qwertyuiop[]", "QWERTYUIOP{}",
                                        "asdfghjkl;'`", "ASDFGHJKL:\"~", "zxcvbnm,./", "ZXCVBNM<>?" };
    static const unsigned char first[] = { 0x02, 0x02, 0x10, 0x10, 0x1E, 0x1E, 0x2C, 0x2C };
    unsigned i;
    const char *p;

    if (c == ' ')
        return 0x39;
    if (c == '\\' || c == '|')
        return 0x2B;
    for (i = 0; i < 8; i++)
        if (c && (p = strchr(rows[i], c)) != 0)
            return first[i] + (unsigned)(p - rows[i]);
    return 0;
}

static int kring_put(struct dos_m *m, unsigned k)
{
    unsigned h = m->khead;

    if (h - m->ktail >= sizeof m->keys / sizeof m->keys[0])
        return 0;
    m->keys[h % (sizeof m->keys / sizeof m->keys[0])] = (unsigned short)k;
    m->khead = h + 1;
    return 1;
}

int dos_key_peek(struct dos_m *m)
{
    return m->khead != m->ktail ? m->keys[m->ktail % (sizeof m->keys / sizeof m->keys[0])] : -1;
}

int dos_key_get(struct dos_m *m)
{
    int k;

    while ((k = dos_key_peek(m)) < 0)
        if (!m->wait || m->wait(m->ctx) < 0)
            return -1;
    m->ktail++;
    return k;
}

int dos_key_push(struct dos_m *m, unsigned key) { return kring_put(m, key) ? 0 : 1; }

/* A VT100 sequence's key: ESC [ n ~ (n), ESC [ X or ESC O X (x: the letter). 0: none. */
static unsigned seq_key(unsigned n, unsigned char x)
{
    static const unsigned short tilde[25] = { 0, 0x4700, 0x5200, 0x5300, 0x4F00, 0x4900, 0x5100, 0x4700, 0x4F00, 0,
                                              0, 0x3B00, 0x3C00, 0x3D00, 0x3E00, 0x3F00, 0, 0x4000, 0x4100, 0x4200,
                                              0x4300, 0x4400, 0, 0x8500, 0x8600 };

    switch (x) {
    case '~': return n < 25 ? tilde[n] : 0;
    case 'A': return 0x4800;
    case 'B': return 0x5000;
    case 'C': return 0x4D00;
    case 'D': return 0x4B00;
    case 'H': return 0x4700;
    case 'F': return 0x4F00;
    case 'P': return 0x3B00;
    case 'Q': return 0x3C00;
    case 'R': return 0x3D00;
    case 'S': return 0x3E00;
    }
    return 0;
}

int dos_type(struct dos_m *m, const unsigned char *b, unsigned n)
{
    unsigned i, k, any = 0;

    for (i = 0; i < n; i++) {
        unsigned char c = b[i];

        if (c == 0x1B && i + 2 < n && (b[i + 1] == '[' || b[i + 1] == 'O')) {
            unsigned j = i + 2, num = 0;

            while (j < n && b[j] >= '0' && b[j] <= '9')
                num = num * 10 + (b[j++] - '0');
            while (j < n && (b[j] == ';' || (b[j] >= '0' && b[j] <= '9')))
                j++;                            /* (a modifier's number: the key as it is unmodified) */
            if (j < n && b[j] >= 0x40 && b[j] <= 0x7E) {
                if ((k = seq_key(num, b[j])) != 0)
                    any |= (unsigned)kring_put(m, k);
                i = j;
                m->after_cr = 0;
                continue;
            }
        }
        if (c == '\n' && m->after_cr) {
            m->after_cr = 0;
            continue;
        }
        m->after_cr = c == '\r';
        if (c == '\r' || c == '\n')
            k = 0x1C0D;
        else if (c == 0x7F || c == 0x08)
            k = 0x0E08;
        else if (c == '\t')
            k = 0x0F09;
        else if (c == 0x1B)
            k = 0x011B;
        else if (c < 0x20)
            k = scan_for((unsigned char)('a' + c - 1)) << 8 | c;
        else if (c < 0x80)
            k = scan_for(c) << 8 | c;
        else
            k = c;
        any |= (unsigned)kring_put(m, k);
    }
    return (int)any;
}

/* Ctrl-C waiting (an output call checks, as every call does with BREAK ON): taken, "^C" said, 1. */
static int break_waiting(struct dos_m *m)
{
    int k = dos_key_peek(m);

    if (k < 0 || (k & 0xFF) != 0x03)
        return 0;
    m->ktail++;
    con_out(m, "^C\r\n", 4);
    return 1;
}

/* A character for 01h, 07h, 08h (an extended key: 00h, then its scan code at the next call): -1 the machine is to
   end, -2 Ctrl-C (check: 01h's and 08h's). */
static int getc_dos(struct dos_m *m, int check, int echo)
{
    int k;
    unsigned c;

    if (m->pend_scan) {
        c = m->pend_scan;
        m->pend_scan = 0;
        return (int)c;
    }
    if ((k = dos_key_get(m)) < 0)
        return -1;
    c = (unsigned)k & 0xFF;
    if (check && c == 0x03) {
        con_out(m, "^C\r\n", 4);
        return -2;
    }
    if (c == 0 || c == 0xE0) {
        m->pend_scan = (unsigned char)(k >> 8);
        return 0;
    }
    if (echo) {
        char e = (char)c;

        con_out(m, &e, 1);
    }
    return (int)c;
}

/* 0Ah's line (buf: max, count, the characters and their CR) from the keys, edited as DOS edits it (Backspace,
   Esc a new start; control characters shown as ^X): 0, -1 the machine is to end, -2 Ctrl-C. */
static int read_line(struct dos_m *m, unsigned char *buf, unsigned max)
{
    unsigned n = m->rl_live && m->rl_n < max ? m->rl_n : 0;   /* (a line whose call went back to wait goes on: its
                                                                   characters are in buf still, echoed) */
    int k;

    m->rl_live = 0;
    if (!max)
        return 0;
    for (;;) {
        unsigned c;

        if ((k = dos_key_get(m)) < 0) {
            m->rl_n = n, m->rl_live = 1;
            return -1;
        }
        c = (unsigned)k & 0xFF;
        if (c == 0x03) {
            con_out(m, "^C\r\n", 4);
            return -2;
        }
        if (c == 0x0D) {
            buf[n] = 0x0D;
            con_out(m, "\r", 1);
            buf[-1] = (unsigned char)n;
            return 0;
        }
        if (c == 0 || c == 0xE0)
            continue;                           /* (F1-F6, the template's keys: to do) */
        if (c == 0x08) {
            if (n) {
                n--;
                con_out(m, buf[n] < 0x20 && buf[n] != '\t' ? "\b \b\b \b" : "\b \b", buf[n] < 0x20 && buf[n] != '\t' ? 6 : 3);
            }
            continue;
        }
        if (c == 0x1B) {
            con_out(m, "\\\r\n", 3);
            n = 0;
            continue;
        }
        if (n + 1 >= max) {
            con_out(m, "\a", 1);
            continue;
        }
        buf[n++] = (unsigned char)c;
        if (c < 0x20 && c != '\t') {
            char e[2];

            e[0] = '^', e[1] = (char)(c + '@');
            con_out(m, e, 2);
        } else {
            char e = (char)c;

            con_out(m, &e, 1);
        }
    }
}

/* 3Fh from CON: raw (its word's bit 5), what's typed as it comes, at least one key; cooked, a line as 0Ah reads
   it with CR LF after it, given out over as many reads as it takes (Ctrl-Z first: the end, 0). Bytes, -1, -2. */
static long con_read(struct dos_m *m, unsigned char *dst, unsigned long cnt, int raw)
{
    unsigned long i = 0;
    int k;

    if (!cnt)
        return 0;
    if (raw) {
        do {
            if ((k = dos_key_get(m)) < 0)
                return i ? (long)i : -1;
            dst[i++] = (unsigned char)k;
        } while (i < cnt && dos_key_peek(m) >= 0);
        return (long)i;
    }
    if (m->line_at >= m->line_n) {
        unsigned char *tmp = m->cooked;         /* (the machine's: a line that waits for a key keeps it) */

        tmp[0] = 128;
        if ((k = read_line(m, tmp + 2, 128)) < 0)
            return k;
        con_out(m, "\n", 1);
        memcpy(m->line, tmp + 2, tmp[1]);
        m->line[tmp[1]] = '\r', m->line[tmp[1] + 1] = '\n';
        m->line_n = tmp[1] + 2u, m->line_at = 0;
        if (tmp[1] && m->line[0] == 0x1A) {
            m->line_n = 0;
            return 0;
        }
    }
    while (i < cnt && m->line_at < m->line_n)
        dst[i++] = (unsigned char)m->line[m->line_at++];
    return (long)i;
}

/* ---- programs */

static unsigned long vec(struct dos_m *m, unsigned n)
{
    return (unsigned long)rd16(m, n * 4) | (unsigned long)rd16(m, n * 4 + 2) << 16;
}

static void set_vec(struct dos_m *m, unsigned n, unsigned long v)
{
    wr16(m, n * 4, (unsigned)v & 0xFFFF);
    wr16(m, n * 4 + 2, (unsigned)(v >> 16) & 0xFFFF);
}

/* The program's name in its block's MCB (DOS 4+: the name without its extension, 8 bytes, NUL-filled). */
static void name_block(struct dos_m *m, unsigned seg, const char *path)
{
    const char *b = path, *p;
    unsigned long mcb = lin(seg - 1, 0);
    unsigned i;

    for (p = path; *p; p++)
        if (*p == '\\' || *p == ':')
            b = p + 1;
    for (i = 0; i < 8; i++)
        wr8(m, mcb + 8 + i, 0);
    for (i = 0; i < 8 && b[i] && b[i] != '.'; i++)
        wr8(m, mcb + 8 + i, (unsigned char)b[i]);
}

static int drive_ok(struct dos_m *m, const unsigned char *fcb)
{
    return !fcb || !fcb[0] || (fcb[0] <= DOS_DRIVES && (m->cds.valid >> (fcb[0] - 1) & 1));
}

/* A program's image: a file on a drive's volume, or bytes in the kernel (dos_m_start_image's). */
struct src {
    struct fsv_file f;
    const unsigned char *buf;
    unsigned long len;
    int drive;
    char full[128];                     /* its whole path on the drive, "\\DIR\\PROG.EXE" */
};

static long src_read(struct src *s, unsigned long off, void *dst, unsigned long n)
{
    if (s->buf) {
        if (off >= s->len)
            return 0;
        if (n > s->len - off)
            n = s->len - off;
        memcpy(dst, s->buf + off, n);
        return (long)n;
    }
    fsv_seek(&s->f, off);
    return fsv_read(&s->f, dst, n);
}

static void src_close(struct src *s)
{
    if (!s->buf)
        fsv_close(&s->f);
}

static unsigned long src_size(struct src *s) { return s->buf ? s->len : fsv_size(&s->f); }

/* Its header read: 0, or 0Bh (and the source closed). */
static int src_header(struct src *s, struct dos_exe *x)
{
    unsigned char head[32];
    long got;

    memset(head, 0, sizeof head);
    got = src_read(s, 0, head, sizeof head);
    if (got < 0 || dos_exe_header(head, (unsigned long)got, src_size(s), x) != 0) {
        src_close(s);
        return 0x0B;                            /* (not a program's format) */
    }
    return 0;
}

/* A program's file opened and its header read. 0 or a DOS error. */
static int open_image(struct dos_m *m, const char *dospath, struct src *s, struct dos_exe *x)
{
    struct fsv_vol *v;
    int e;

    memset(s, 0, sizeof *s);
    if ((e = resolve_str(m, dospath, &v, &s->drive, s->full)) != 0)
        return e;
    if ((e = fsv_open(v, s->full, FSV_READ, FSV_DENY_WRITE, &s->f)) != 0)
        return no_file(v, s->full, fs_err(e, 0x02));
    return src_header(s, x);
}

/* Its image read to segment at_seg (its block ending at end, linear) and an .EXE's relocations by factor. 0 or a
   DOS error. */
static int load_image(struct dos_m *m, struct src *s, const struct dos_exe *x, unsigned at_seg,
                      unsigned long end, unsigned factor)
{
    unsigned char rel[4 * 64];
    unsigned long at = lin(at_seg, 0), n = x->image_len, i;

    if (end > m->size)
        end = m->size;
    if (at >= end)
        return 0x08;
    if (n > end - at)
        n = end - at;
    if (src_read(s, x->image_off, m->mem + at, n) < 0)
        return 0x1F;
    for (i = 0; x->exe && i < x->nrel; i += 64) {
        unsigned c = (unsigned)(x->nrel - i < 64 ? x->nrel - i : 64);

        if (src_read(s, x->rel_off + i * 4, rel, (unsigned long)c * 4) != (long)c * 4)
            return 0x0B;
        dos_exe_relocate(m->mem + at, end - at, rel, c, factor);
    }
    return 0;
}

/* Load and start a program (its source open, its header x) as parent's child: env (0: a copy of the parent's),
   tail, the two FCBs (16 bytes or 0), ret the parent's return point (INT 22h for the child). The source closed;
   r: the child's first registers. 0 or a DOS error. */
static int exec_src(struct dos_m *m, unsigned parent, struct src *s, const struct dos_exe *xx, unsigned env,
                    const char *tail, const unsigned char *fcb1, const unsigned char *fcb2, unsigned long ret,
                    struct dos_regs86 *r)
{
    char whole[132];
    struct dos_exe x = *xx;
    struct dos_regs0 r0;
    struct dos_psp_init pin;
    unsigned char jft[20];
    unsigned long size = src_size(s), need, want, envlen = 0, room;
    unsigned seg, paras, largest, envseg, src, img;
    const unsigned char *sp;
    const char *full = s->full;
    int drive = s->drive, e;

    /* Its environment: the parent's strings (or those given) and the program's whole name. */
    src = env ? env : rd16(m, lin(parent, 0x2C));
    sp = src && lin(src, 0) < m->size ? m->mem + lin(src, 0) : 0;
    while (sp && lin(src, 0) + envlen + 1 < m->size && envlen < 32767
           && !(sp[envlen] == 0 && (envlen == 0 || sp[envlen - 1] == 0)))
        envlen++;
    whole[0] = (char)('A' + drive), whole[1] = ':';
    memcpy(whole + 2, full, strlen(full) + 1);
    room = envlen + 2 + 2 + strlen(whole) + 1;
    if (dos_mem_alloc(&m->mcb, (unsigned)((room + 15) / 16), parent, &envseg, &largest) != 0) {
        src_close(s);
        return 0x08;
    }
    if (!dos_env_make(m->mem + lin(envseg, 0), (room + 15) / 16 * 16, sp, sp ? envlen + 2 : 0, whole)) {
        dos_mem_free(&m->mcb, envseg);
        src_close(s);
        return 0x0A;                            /* (a bad environment) */
    }
    /* Its block: as much as it wants, or the largest there is if no less than it needs. */
    dos_exe_memory(&x, size, &need, &want);
    dos_mem_alloc(&m->mcb, 0xFFFF, parent, &seg, &largest);
    paras = (unsigned)(want < largest ? want : largest);
    if (largest < need || dos_mem_alloc(&m->mcb, paras, parent, &seg, &largest) != 0) {
        dos_mem_free(&m->mcb, envseg);
        src_close(s);
        return 0x08;
    }
    img = dos_exe_image_seg(&x, seg, paras);
    e = load_image(m, s, &x, img, lin(seg, 0) + (unsigned long)paras * 16, img);
    src_close(s);
    if (e) {
        dos_mem_free(&m->mcb, seg);
        dos_mem_free(&m->mcb, envseg);
        return e;
    }
    /* Its PSP: the handles it inherits, the vectors to give back at its end, its tail and FCBs. */
    dos_inherit(&m->files, parent, jft);
    memset(&pin, 0, sizeof pin);
    pin.seg = seg;
    pin.top = seg + paras;
    pin.parent = parent;
    pin.env = envseg;
    pin.jft = jft;
    pin.int22 = ret;
    pin.int23 = vec(m, 0x23);
    pin.int24 = vec(m, 0x24);
    pin.version = 0x0A07;
    pin.fcb1 = fcb1;
    pin.fcb2 = fcb2;
    pin.tail = tail ? tail : "";
    dos_psp_make(m->mem + lin(seg, 0), &pin);
    wr16(m, lin(seg - 1, 0) + 1, seg);          /* (its block and its environment are its own) */
    wr16(m, lin(envseg - 1, 0) + 1, seg);
    name_block(m, seg, full);
    set_vec(m, 0x22, ret);
    dos_exe_regs(&x, seg, paras, drive_ok(m, fcb1), drive_ok(m, fcb2), &r0);
    if (r0.push0)
        wr16(m, lin(r0.ss, r0.sp), 0);
    memset(r, 0, sizeof *r);
    r->ax = r0.ax;
    r->cx = 0xFF;                               /* (what DOS leaves: to check) */
    r->cs = r0.cs, r->ip = r0.ip, r->ss = r0.ss, r->sp = r0.sp, r->ds = r0.ds, r->es = r0.es;
    r->flags = 0x0202;
    set_psp(m, seg);
    set_dta(m, seg, 0x80);
    return 0;
}

static int exec(struct dos_m *m, unsigned parent, const char *dospath, unsigned env, const char *tail,
                const unsigned char *fcb1, const unsigned char *fcb2, unsigned long ret, struct dos_regs86 *r)
{
    struct src s;
    struct dos_exe x;
    int e;

    if ((e = open_image(m, dospath, &s, &x)) != 0)
        return e;
    return exec_src(m, parent, &s, &x, env, tail, fcb1, fcb2, ret, r);
}

/* 4B03h: an overlay's image at the segment given, relocated by the factor given; no PSP, no memory of its own. */
static int overlay(struct dos_m *m, const char *dospath, unsigned at, unsigned factor)
{
    struct src s;
    struct dos_exe x;
    int e;

    if ((e = open_image(m, dospath, &s, &x)) != 0)
        return e;
    e = load_image(m, &s, &x, at, m->size, factor);
    src_close(&s);
    return e;
}

/* The running program ends (code, kind: AH of 4Dh): its handles closed, its memory freed, the vectors it had
   given back; the parent runs on where its EXEC was, or the machine ends with the root's child. */
static int end_program(struct dos_m *m, struct dos_regs86 *r, unsigned code, unsigned kind)
{
    unsigned cur = cur_psp(m), parent = rd16(m, lin(cur, 0x16));
    unsigned long ret = (unsigned long)rd16(m, lin(cur, 0x0A)) | (unsigned long)rd16(m, lin(cur, 0x0C)) << 16;

    m->exit_code = (kind & 0xFF) << 8 | (code & 0xFF);
    set_vec(m, 0x22, ret);
    set_vec(m, 0x23, (unsigned long)rd16(m, lin(cur, 0x0E)) | (unsigned long)rd16(m, lin(cur, 0x10)) << 16);
    set_vec(m, 0x24, (unsigned long)rd16(m, lin(cur, 0x12)) | (unsigned long)rd16(m, lin(cur, 0x14)) << 16);
    if (kind == 3) {                            /* (31h: its memory as it asked, its files open: resident) */
        unsigned largest, paras = r->dx < 6 ? 6 : r->dx;

        dos_mem_resize(&m->mcb, cur, paras, &largest);
    } else {
        unsigned k;

        dos_close_all(&m->files, cur);
        for (k = 0; k < DOS_LFN_FINDS; k++)     /* (its long-name searches left open) */
            if (m->lfn[k].used && m->lfn[k].psp == cur)
                m->lfn[k].used = 0;
        dos_mem_free_owner(&m->mcb, cur);
    }
    if ((parent == m->lay.root_psp && !m->root_runs) || parent == cur || cur == m->lay.root_psp) {
        set_psp(m, m->lay.root_psp);
        set_dta(m, m->lay.root_psp, 0x80);
        m->ended = 1;
        return DOS21_END;
    }
    set_psp(m, parent);
    set_dta(m, parent, 0x80);
    r->cs = (unsigned)(ret >> 16) & 0xFFFF, r->ip = (unsigned)ret & 0xFFFF;
    r->ss = rd16(m, lin(parent, 0x30)), r->sp = rd16(m, lin(parent, 0x2E));
    r->flags &= ~CF;
    return DOS21_SWITCH;
}

/* ---- the machine */

static void lol_make(struct dos_m *m);

void dos_m_init(struct dos_m *m, unsigned char *mem, unsigned long size, const struct dos_layout *lay,
                struct fsv_vol *v, int drive, int cp)
{
    static const char env[] = "PATH=C:\\\0COMSPEC=C:\\COMMAND.COM\0";
    struct dos_psp_init pin;
    unsigned envseg, largest, i;

    memset(m, 0, sizeof *m);
    m->mem = mem;
    m->size = size;
    m->lay = *lay;
    m->cp = cp ? cp : 437;
    m->vol[drive] = v;
    m->cds.valid = v ? 1u << drive : 0;
    m->cds.current = drive;
    for (i = 0; i < DOS_DRIVES; i++)
        strcpy(m->cds.cwd[i], "\\");
    memset(mem + lay->sda, 0, DOS_SDA_SIZE);
    memset(mem + lay->misc, 0, 6);
    mem[lay->misc + 2] = 0xCB;                  /* (RETF) */
    lol_make(m);
    dos_files_init(&m->files, mem, m->lay.sft, DOS_FILES);
    dos_mem_init(&m->mcb, mem, size, m->lay.first_mcb, m->lay.low_top, 0, 0);
    dos_mem_alloc(&m->mcb, (sizeof env + 1 + 15) / 16, m->lay.root_psp, &envseg, &largest);
    memcpy(mem + lin(envseg, 0), env, sizeof env);      /* (its strings, then the 0 that ends them) */
    mem[lin(envseg, 0) + sizeof env] = 0;
    memset(&pin, 0, sizeof pin);
    pin.seg = m->lay.root_psp;
    pin.top = m->lay.low_top;
    pin.parent = m->lay.root_psp;
    pin.env = envseg;
    pin.version = 0x0A07;
    pin.tail = "";
    dos_psp_make(mem + lin(m->lay.root_psp, 0), &pin);
    dos_files_first_jft(&m->files, m->lay.root_psp);
    set_psp(m, m->lay.root_psp);
    set_dta(m, m->lay.root_psp, 0x80);
    wr8(m, m->lay.sda + 0x16, (unsigned)drive);
}

int dos_m_init_vm(struct dos_m *m, unsigned char *mem, unsigned long size, unsigned psp, unsigned top,
                  unsigned low_top, struct fsv_vol *v, int drive, int cp, unsigned extra, unsigned *extra_seg)
{
    unsigned long bytes = 0x12 + 0x70 + DOS_SDA_SIZE + 0x20 + 6 + (unsigned long)DOS_FILES * DOS_SFT_SIZE;
    unsigned paras = (unsigned)((bytes + 15) / 16), seg, largest, i;
    struct dos_psp_init pin;
    unsigned long base;

    memset(m, 0, sizeof *m);
    m->mem = mem;
    m->size = size;
    m->cp = cp ? cp : 437;
    if (psp < 0x40 || top <= psp || (unsigned long)low_top * 16 > size || top + paras + 2 > low_top)
        return -1;
    /* the chain: the program's block, then everything free, then DOS's tables at the top (last fit) */
    dos_mem_init(&m->mcb, mem, size, psp - 1, low_top, 0, 0);
    if (dos_mem_alloc(&m->mcb, top - psp, psp, &seg, &largest) != 0 || seg != psp)
        return -1;
    dos_mem_strategy(&m->mcb, 0x02);
    if (dos_mem_alloc(&m->mcb, paras, 0x0008, &seg, &largest) != 0)
        return -1;
    if (extra && dos_mem_alloc(&m->mcb, extra, 0x0008, extra_seg, &largest) != 0)
        return -1;
    dos_mem_strategy(&m->mcb, 0x00);
    for (i = 0; i < 8; i++)                     /* (its name, as DOS's own blocks have "SC" or "SD") */
        mem[lin(seg - 1, 8) + i] = (unsigned char)"SD\0\0\0\0\0\0"[i];
    base = lin(seg, 0);
    memset(mem + base, 0, (unsigned long)paras * 16);
    m->lay.lol = base + 0x12;
    m->lay.sda = m->lay.lol + 0x70;
    m->lay.misc = m->lay.sda + DOS_SDA_SIZE;
    m->lay.sft = m->lay.misc + 0x20;            /* (misc: 32 bytes here, the last 16 the system VM's own: vdos.c) */
    m->lay.root_psp = psp;
    m->lay.first_mcb = psp - 1;
    m->lay.low_top = low_top;
    m->vol[drive] = v;
    m->cds.valid = v ? 1u << drive : 0;
    m->cds.current = drive;
    for (i = 0; i < DOS_DRIVES; i++)
        strcpy(m->cds.cwd[i], "\\");
    mem[m->lay.misc + 2] = 0xCB;                /* (RETF) */
    lol_make(m);
    dos_files_init(&m->files, mem, m->lay.sft, DOS_FILES);
    memset(&pin, 0, sizeof pin);
    pin.seg = psp;
    pin.top = low_top;
    pin.parent = psp;
    pin.version = 0x0A07;
    pin.tail = "";
    dos_psp_make(mem + lin(psp, 0), &pin);
    dos_files_first_jft(&m->files, psp);
    set_psp(m, psp);
    set_dta(m, psp, 0x80);
    wr8(m, m->lay.sda + 0x16, (unsigned)drive);
    m->root_runs = 1;
    return 0;
}

int dos_m_start(struct dos_m *m, const char *path, const char *tail, struct dos_regs86 *r)
{
    m->ended = 0;
    return exec(m, m->lay.root_psp, path, 0, tail, 0, 0, 0, r);
}

int dos_m_start_image(struct dos_m *m, const char *name, const unsigned char *image, unsigned long len,
                      const char *tail, struct dos_regs86 *r)
{
    struct src s;
    struct dos_exe x;
    unsigned i;
    int e;

    memset(&s, 0, sizeof s);
    s.buf = image, s.len = len, s.drive = m->cds.current;
    s.full[0] = '\\';
    for (i = 0; name[i] && i < sizeof s.full - 2; i++)
        s.full[i + 1] = name[i];
    if ((e = src_header(&s, &x)) != 0)
        return e;
    m->ended = 0;
    return exec_src(m, m->lay.root_psp, &s, &x, 0, tail, 0, 0, 0, r);
}

void dos_m_close(struct dos_m *m)
{
    unsigned i;
    int d;

    for (i = 0; i < m->files.n && i < DOS_SFT_MAX; i++)
        if (!m->files.k[i].dev && m->files.k[i].f.n)
            fsv_close(&m->files.k[i].f);
    for (d = 0; d < DOS_DRIVES; d++)
        if (m->vol[d])
            fsv_flush(m->vol[d]);
}

int dos20(struct dos_m *m, struct dos_regs86 *r)
{
    return end_program(m, r, 0, 0);
}

/* ---- INT 21h's groups */

static int file_io(struct dos_m *m, struct dos_regs86 *r, unsigned ah)
{
    unsigned long a = lin(r->ds, r->dx), cnt = r->cx;
    int i = dos_entry(&m->files, cur_psp(m), r->bx);
    long got;

    if (i < 0)
        return err(m, r, 0x06);
    if (a >= m->size)
        cnt = 0;
    else if (cnt > m->size - a)
        cnt = m->size - a;
    switch (m->files.k[i].dev) {
    case DOS_DEV_CON: {
        int raw = (rd16(m, m->files.table + 6 + (unsigned long)i * DOS_SFT_SIZE + 5) & 0x20) != 0;

        if (ah == 0x40) {
            if (!raw && break_waiting(m))
                return DOS21_CTRLC;
            con_out(m, (const char *)m->mem + a, (unsigned)cnt);
            r->ax = (unsigned)cnt;
            return ok(r);
        }
        got = con_read(m, m->mem + a, cnt, raw);
        if (got == -1)
            return DOS21_GONE;
        if (got == -2)
            return DOS21_CTRLC;
        r->ax = (unsigned)got;
        return ok(r);
    }
    case DOS_DEV_AUX:
    case DOS_DEV_PRN:
        r->ax = ah == 0x40 ? (unsigned)cnt : 0; /* (no ports: what's written goes nowhere) */
        return ok(r);
    }
    got = ah == 0x3F ? dos_read(&m->files, cur_psp(m), r->bx, m->mem + a, cnt)
                     : dos_write(&m->files, cur_psp(m), r->bx, m->mem + a, cnt);
    if (got < 0)
        return err(m, r, (unsigned)-got);
    r->ax = (unsigned)got;
    return ok(r);
}

static int serial(struct dos_m *m, struct dos_regs86 *r);

static int ioctl(struct dos_m *m, struct dos_regs86 *r, unsigned al)
{
    unsigned long en;
    int i, d;

    switch (al) {
    case 0x00:
    case 0x01:
    case 0x06:
    case 0x07:
        if ((i = dos_entry(&m->files, cur_psp(m), r->bx)) < 0)
            return err(m, r, 0x06);
        en = m->files.table + 6 + (unsigned long)i * DOS_SFT_SIZE;
        if (al == 0x00) {
            r->dx = rd16(m, en + 5);
        } else if (al == 0x01) {
            if (!(rd16(m, en + 5) & 0x80))
                return err(m, r, 0x01);         /* (a file's word isn't set) */
            wr8(m, en + 5, (rd16(m, en + 5) & 0xDF) | (r->dx & 0x20));     /* (raw or cooked) */
        } else if (al == 0x06) {
            if (m->files.k[i].dev == DOS_DEV_CON) {
                if (m->fill)
                    m->fill(m->ctx, 1);
                set_al(r, dos_key_peek(m) >= 0 || m->line_at < m->line_n ? 0xFF : 0);
                if (!(r->ax & 0xFF) && m->idle)
                    m->idle(m->ctx);
            } else
                set_al(r, m->files.k[i].dev || m->files.k[i].f.pos < fsv_size(&m->files.k[i].f) ? 0xFF : 0);
        } else {
            set_al(r, 0xFF);
        }
        return ok(r);
    case 0x10:
    case 0x11:                                  /* (generic requests: none supported) */
        return err(m, r, 0x01);
    case 0x0D:                                  /* a drive's generic requests: 66h alone, the volume's serial
                                                   number, label and file system at DS:DX (BL the drive), which is
                                                   6900h's answer; category 08h, or FAT32's 48h */
        if ((r->cx & 0xFF) == 0x66 && ((r->cx >> 8) == 0x08 || (r->cx >> 8) == 0x48))
            return serial(m, r);
        return err(m, r, 0x01);
    case 0x08:
    case 0x09:
    case 0x0E:
        d = (r->bx & 0xFF) ? (int)(r->bx & 0xFF) - 1 : m->cds.current;
        if (d < 0 || d >= DOS_DRIVES || !m->vol[d])
            return err(m, r, 0x0F);
        if (al == 0x08)
            r->ax = 1;                          /* (fixed) */
        else if (al == 0x09)
            r->dx = 0;                          /* (local) */
        else
            set_al(r, 0);                       /* (one letter for the drive) */
        return ok(r);
    }
    return DOS21_UNANSWERED;
}

static int find(struct dos_m *m, struct dos_regs86 *r, unsigned ah)
{
    unsigned long dl = lin(dta_seg(m), dta_off(m));
    unsigned char *dta = m->mem + dl;
    char spec[128], dir[128], path[128];
    const char *last;
    struct fsv_vol *v;
    int drive, e, k;

    if (dl + DOS_DTA_FIND > m->size)
        return err(m, r, 0x09);
    if (ah == 0x4F) {
        if (dta[0] >= DOS_DRIVES || !(v = m->vol[dta[0]]))
            return err(m, r, 0x12);
        return done(m, r, dos_findnext(v, dta));
    }
    if (str_in(m, r->ds, r->dx, spec, sizeof spec) != 0)
        return err(m, r, 0x03);
    /* the folder (what comes before the last part), resolved; the last part the template */
    for (last = spec, k = 0; spec[k]; k++)
        if (spec[k] == '\\' || spec[k] == '/' || (spec[k] == ':' && k == 1))
            last = spec + k + 1;
    memcpy(dir, spec, (unsigned long)(last - spec));
    dir[last - spec] = 0;
    if (!dir[0] || dir[last - spec - 1] == ':')
        dir[last - spec] = '.', dir[last - spec + 1] = 0;
    e = resolve_str(m, dir, &v, &drive, path);
    if (e)
        return err(m, r, e == 0x02 ? 0x03 : (unsigned)e);
    return done(m, r, dos_findfirst(v, (unsigned)drive, path, *last ? last : "*.*", r->cx & 0xFF, m->cp, dta));
}

/* 29h: a name at DS:SI parsed into the FCB at ES:DI, as MS-DOS 2-7 parse it (to check against 7.1): blanks
   skipped, and one separator with AL bit 0; a drive (left as it was with bit 1 if none is given), up to 8 name
   and 3 extension characters to a terminator, upper-cased, '*' the rest of the field as '?', a field not given
   left as it was with bit 2 (name) or 3 (extension), or else blanks. AL: 00h, 01h wildcards, FFh a bad drive;
   DS:SI past what was parsed. */
static int fcb_term(unsigned c) { return c < 0x21 || (c < 0x80 && strchr(":.;,=+/\"[]<>|", (int)c) != 0); }

static void fcb_field(struct dos_m *m, unsigned seg, unsigned *si, unsigned long at, unsigned n, int keep,
                      unsigned *wild)
{
    unsigned k = 0, c, any = 0;

    while ((c = rd8(m, lin(seg, *si))) != 0 && !fcb_term(c)) {
        any = 1;
        if (c == '*') {
            for (; k < n; k++)
                wr8(m, at + k, '?');
            *wild = 1;
        } else if (k < n) {
            if (c == '?')
                *wild = 1;
            wr8(m, at + k++, dos_upper(m->cp, (unsigned char)c));
        }
        (*si)++;
    }
    if (!any && keep)
        return;
    for (; k < n; k++)
        wr8(m, at + k, ' ');
}

static int parse_fcb(struct dos_m *m, struct dos_regs86 *r, unsigned al)
{
    unsigned long f = lin(r->es, r->di);
    unsigned si = r->si, seg = r->ds, c, d, wild = 0, bad = 0;

    while ((c = rd8(m, lin(seg, si))) == ' ' || c == '\t')
        si++;
    if ((al & 1) && c && strchr(":.;,=+", (int)c)) {
        si++;
        while ((c = rd8(m, lin(seg, si))) == ' ' || c == '\t')
            si++;
    }
    d = dos_upper(m->cp, (unsigned char)rd8(m, lin(seg, si)));
    if (d >= 'A' && d <= 'Z' && rd8(m, lin(seg, si + 1)) == ':') {
        wr8(m, f, d - 'A' + 1);
        bad = !(m->cds.valid >> (d - 'A') & 1);
        si += 2;
    } else if (!(al & 2)) {
        wr8(m, f, 0);
    }
    fcb_field(m, seg, &si, f + 1, 8, al & 4, &wild);
    if (rd8(m, lin(seg, si)) == '.') {
        si++;
        fcb_field(m, seg, &si, f + 9, 3, al & 8, &wild);
    } else if (!(al & 8)) {
        for (c = 0; c < 3; c++)
            wr8(m, f + 9 + c, ' ');
    }
    r->si = si;
    set_al(r, bad ? 0xFF : wild ? 1 : 0);
    return DOS21_DONE;
}

/* 6Ch: DS:SI's name opened, made or replaced by DX's actions (bits 0-3 if it is there: 0 fail, 1 open, 2
   replace; bits 4-7 if not: 0 fail, 1 make), BL the mode, CX the new file's attributes. AX the handle, CX what was
   done (1 opened, 2 made, 3 replaced). */
static int ext_open(struct dos_m *m, struct dos_regs86 *r)
{
    char p[128], path[128];
    struct fsv_vol *v;
    struct fat_ent fe;
    unsigned h, mode = r->bx & 0xFF, there_act = r->dx & 0x0F, none_act = (r->dx >> 4) & 0x0F, done_ = 1;
    int drive, e, d, there;

    if (str_in(m, r->ds, r->si, p, sizeof p) != 0)
        return err(m, r, 0x03);
    if ((d = dev_name(p)) != 0) {
        if ((e = dos_open(&m->files, cur_psp(m), 0, 0, "", d, mode, &h)) != 0)
            return err(m, r, (unsigned)e);
        r->ax = h, r->cx = 1;
        return ok(r);
    }
    if ((e = resolve_str(m, p, &v, &drive, path)) != 0)
        return err(m, r, (unsigned)e);
    there = fsv_lookup(v, path, &fe) == 0;
    if (there && (fe.attr & (FAT_A_DIR | FAT_A_LABEL)))
        return err(m, r, 0x05);
    if (there ? there_act == 0 : none_act == 0)
        return err(m, r, there ? 0x50 : (unsigned)no_file(v, path, 0x02));
    if (there_act > 2 || none_act > 1)
        return err(m, r, 0x01);                 /* (MS-DOS 7.1's answer) */
    if (there && there_act == 1)
        e = dos_open(&m->files, cur_psp(m), v, (unsigned)drive, path, 0, mode, &h);
    else
        e = dos_create(&m->files, cur_psp(m), v, (unsigned)drive, path, (unsigned char)r->cx, there, &h),
        done_ = there ? 3 : 2;
    if (e)
        return err(m, r, (unsigned)no_file(v, path, e));
    r->ax = h, r->cx = done_;
    return ok(r);
}

/* 52h's List of Lists, made once at the machine's start: the first MCB, the file table, NUL's header, the
   drives (to check which of its other fields programs read: DOS/4GW, the census says). */
static void lol_make(struct dos_m *m)
{
    unsigned long l = m->lay.lol;
    unsigned i;

    for (i = 0; i < 0x80; i++)
        wr8(m, l - 0x12 + i, 0);
    wr16(m, l - 2, m->lay.first_mcb);
    wr16(m, l + 0x04, (unsigned)(m->lay.sft & 0xF)), wr16(m, l + 0x06, (unsigned)(m->lay.sft >> 4));
    wr16(m, l + 0x10, 512);                     /* (the largest sector) */
    wr8(m, l + 0x20, 1);                        /* (block devices) */
    wr8(m, l + 0x21, DOS_DRIVES);               /* (LASTDRIVE: Z:, as 0Eh says) */
    wr16(m, l + 0x22, 0xFFFF), wr16(m, l + 0x24, 0xFFFF);      /* NUL's header: the last device */
    wr16(m, l + 0x26, 0x8004);                  /* (a character device, NUL) */
    for (i = 0; i < 8; i++)
        wr8(m, l + 0x2C + i, (unsigned char)"NUL     "[i]);
}

/* 7303h: FAT32's free space for DS:DX's root ("C:\\"), at ES:DI (CX bytes). */
static int free32(struct dos_m *m, struct dos_regs86 *r)
{
    unsigned long b = lin(r->es, r->di), fr, tot;
    struct fsv_vol *v;
    char p[8];
    int d;

    if (str_in(m, r->ds, r->dx, p, sizeof p) != 0 || r->cx < 0x2C)
        return err(m, r, 0x18);
    d = p[0] && p[1] == ':' ? (int)(dos_upper(m->cp, (unsigned char)p[0]) - 'A') : m->cds.current;
    if (d < 0 || d >= DOS_DRIVES || !(v = m->vol[d]) || fsv_free(v, &fr) != 0)
        return err(m, r, 0x0F);
    tot = v->fat.clusters;
    wr16(m, b, 0x24), wr16(m, b + 2, 0);
    wr16(m, b + 4, v->fat.spc), wr16(m, b + 6, 0);
    wr16(m, b + 8, 512), wr16(m, b + 10, 0);
    wr16(m, b + 0x0C, (unsigned)fr), wr16(m, b + 0x0E, (unsigned)(fr >> 16));
    wr16(m, b + 0x10, (unsigned)tot), wr16(m, b + 0x12, (unsigned)(tot >> 16));
    fr *= v->fat.spc, tot *= v->fat.spc;
    wr16(m, b + 0x14, (unsigned)fr), wr16(m, b + 0x16, (unsigned)(fr >> 16));
    wr16(m, b + 0x18, (unsigned)tot), wr16(m, b + 0x1A, (unsigned)(tot >> 16));
    fr /= v->fat.spc, tot /= v->fat.spc;
    wr16(m, b + 0x1C, (unsigned)fr), wr16(m, b + 0x1E, (unsigned)(fr >> 16));
    wr16(m, b + 0x20, (unsigned)tot), wr16(m, b + 0x22, (unsigned)(tot >> 16));
    return ok(r);
}

/* 6900h: BL's drive's serial number, label and file system's name at DS:DX (from its boot sector). */
static int serial(struct dos_m *m, struct dos_regs86 *r)
{
    unsigned long b = lin(r->ds, r->dx);
    int d = (r->bx & 0xFF) ? (int)(r->bx & 0xFF) - 1 : m->cds.current;
    struct fsv_vol *v;
    unsigned i;

    if (d < 0 || d >= DOS_DRIVES || !(v = m->vol[d]))
        return err(m, r, 0x0F);
    wr16(m, b, 0);                              /* (info level 0) */
    wr16(m, b + 2, (unsigned)v->fat.serial), wr16(m, b + 4, (unsigned)(v->fat.serial >> 16));
    for (i = 0; i < 11; i++)                    /* (the label blank-filled: trailing blanks were dropped) */
        wr8(m, b + 6 + i, (unsigned char)(i < strlen(v->fat.label) ? v->fat.label[i] : ' '));
    for (i = 0; i < 8; i++)
        wr8(m, b + 0x11 + i, (unsigned char)(v->fat.fat == 32 ? "FAT32   " : v->fat.fat == 16 ? "FAT16   "
                                                                                               : "FAT12   ")[i]);
    return ok(r);
}

/* 7302h: DL's drive's extended DPB at ES:DI (CX bytes, 3Fh at least), as MS-DOS 7.1 gives it for any FAT drive:
   the length (3Dh), DOS 4's DPB from the volume's parameters (FAT32's 16-bit fields 0), then FAT32's fields. */
static int ext_dpb(struct dos_m *m, struct dos_regs86 *r)
{
    unsigned long b = lin(r->es, r->di), p = b + 2;
    int d = (r->dx & 0xFF) ? (int)(r->dx & 0xFF) - 1 : m->cds.current;
    const struct fat_vol *f;
    unsigned shift = 0, i, big;

    if (r->cx < 0x3F)
        return err(m, r, 0x18);
    if (d < 0 || d >= DOS_DRIVES || !m->vol[d])
        return err(m, r, 0x0F);
    f = &m->vol[d]->fat;
    big = f->fat == 32;
    while ((1u << shift) < f->spc && shift < 8)
        shift++;
    for (i = 0; i < 0x3F; i++)
        wr8(m, b + i, 0);
    wr16(m, b, 0x3D);
    wr8(m, p + 0x00, (unsigned)d), wr8(m, p + 0x01, (unsigned)d);
    wr16(m, p + 0x02, 512);
    wr8(m, p + 0x04, f->spc - 1), wr8(m, p + 0x05, shift);
    wr16(m, p + 0x06, (unsigned)f->fat0);
    wr8(m, p + 0x08, f->nfats);
    wr16(m, p + 0x09, big ? 0 : (unsigned)(f->root_sectors * 16));
    wr16(m, p + 0x0B, big ? 0 : (unsigned)f->data_start);
    wr16(m, p + 0x0D, big ? 0 : (unsigned)(f->clusters + 1));
    wr16(m, p + 0x0F, big ? 0 : (unsigned)f->fat_sectors);
    wr16(m, p + 0x11, big ? 0 : (unsigned)f->root_start);
    wr8(m, p + 0x17, 0xF8);                     /* (a fixed disk's media byte) */
    wr16(m, p + 0x19, 0xFFFF), wr16(m, p + 0x1B, 0xFFFF);      /* (the last DPB) */
    wr16(m, p + 0x1D, (unsigned)f->next_free);
    wr16(m, p + 0x1F, f->free_count == 0xFFFFFFFFUL ? 0xFFFF : (unsigned)f->free_count);
    wr16(m, p + 0x21, f->free_count == 0xFFFFFFFFUL ? 0xFFFF : (unsigned)(f->free_count >> 16));
    wr16(m, p + 0x25, big ? (unsigned)f->fsinfo : 0xFFFF);
    wr16(m, p + 0x27, big ? 6 : 0xFFFF);
    wr16(m, p + 0x29, (unsigned)f->data_start), wr16(m, p + 0x2B, (unsigned)(f->data_start >> 16));
    wr16(m, p + 0x2D, (unsigned)(f->clusters + 1)), wr16(m, p + 0x2F, (unsigned)((f->clusters + 1) >> 16));
    wr16(m, p + 0x31, (unsigned)f->fat_sectors), wr16(m, p + 0x33, (unsigned)(f->fat_sectors >> 16));
    wr16(m, p + 0x35, (unsigned)f->root_cluster), wr16(m, p + 0x37, (unsigned)(f->root_cluster >> 16));
    wr16(m, p + 0x39, (unsigned)f->next_free), wr16(m, p + 0x3B, (unsigned)(f->next_free >> 16));
    return ok(r);
}

static int country(struct dos_m *m, struct dos_regs86 *r)
{
    unsigned long b = lin(r->ds, r->dx);
    unsigned i;

    if ((r->dx & 0xFFFF) == 0xFFFF)
        return err(m, r, 0x02);                 /* (setting a country: not this DOS's) */
    for (i = 0; i < 34; i++)
        wr8(m, b + i, 0);
    wr16(m, b + 0x00, 0);                       /* (month, day, year: the USA's, as code page 437 is) */
    wr8(m, b + 0x02, '$');
    wr8(m, b + 0x07, ',');
    wr8(m, b + 0x09, '.');
    wr8(m, b + 0x0B, '-');
    wr8(m, b + 0x0D, ':');
    wr8(m, b + 0x10, 2);
    wr16(m, b + 0x12, (unsigned)(m->lay.misc + 2) & 0xF);  /* (the case map: a RETF, to check) */
    wr16(m, b + 0x14, (unsigned)(m->lay.misc + 2) >> 4);
    wr8(m, b + 0x16, ',');
    r->bx = 1;
    return ok(r);
}

static int exec_call(struct dos_m *m, struct dos_regs86 *r, unsigned al)
{
    unsigned long pb = lin(r->es, r->bx), f1, f2;
    unsigned env, toff, tseg, len, i, parent = cur_psp(m);
    unsigned char fcb1[16], fcb2[16];
    char path[128], tail[128];
    struct dos_regs86 child;
    int e;

    if (str_in(m, r->ds, r->dx, path, sizeof path) != 0)
        return err(m, r, 0x02);
    if (al == 0x03)
        return done(m, r, overlay(m, path, rd16(m, pb), rd16(m, pb + 2)));
    env = rd16(m, pb), toff = rd16(m, pb + 2), tseg = rd16(m, pb + 4);
    f1 = lin(rd16(m, pb + 8), rd16(m, pb + 6)), f2 = lin(rd16(m, pb + 12), rd16(m, pb + 10));
    len = rd8(m, lin(tseg, toff));
    for (i = 0; i < len && i < 126; i++)
        tail[i] = (char)rd8(m, lin(tseg, toff + 1 + i));
    tail[i] = 0;
    for (i = 0; i < 16; i++)
        fcb1[i] = (unsigned char)rd8(m, f1 + i), fcb2[i] = (unsigned char)rd8(m, f2 + i);
    /* the parent's SS:SP kept in its PSP, as DOS keeps them, for its child's end */
    wr16(m, lin(parent, 0x2E), r->sp);
    wr16(m, lin(parent, 0x30), r->ss);
    if ((e = exec(m, parent, path, env, tail, fcb1, fcb2, (unsigned long)r->cs << 16 | r->ip, &child)) != 0)
        return err(m, r, (unsigned)e);
    if (al == 0x01) {                           /* (loaded, not run: its first SS:SP, with AX on its stack, and
                                                    CS:IP in the block; it is the current process) */
        child.sp = (child.sp - 2) & 0xFFFF;
        wr16(m, lin(child.ss, child.sp), child.ax);
        wr16(m, pb + 0x0E, child.sp), wr16(m, pb + 0x10, child.ss);
        wr16(m, pb + 0x12, child.ip), wr16(m, pb + 0x14, child.cs);
        return ok(r);
    }
    *r = child;
    return DOS21_SWITCH;
}


/* ---- long names (71xxh: Windows 95's calls, which MS-DOS 7.1 on its own refuses; GLOS's own DOS answers them, so
   that programs under a native GLOS have long names with no Windows: docs/v1/native-dos.md, N2)

   Paths are the code page's bytes, resolved whole with their parts as given (path.c), then UTF-8 for kernel/fs,
   which finds a part by its long or its short name. A drive's current folder stays as the CDS has it, short:
   713Bh stores each part's short name, 7147h gives the long ones back. Searches are handles (714Eh, 714Fh,
   71A1h) on a folder being listed; a pattern is matched against the long name and the short one, without regard
   to case in ASCII. Times in a search's record are DOS's packed date and time if the caller asks so (SI 1), else
   Windows's 64-bit count of 100 ns since 1601 (the local time taken as it is). */

static void wr32(struct dos_m *m, unsigned long a, unsigned long v)
{
    wr16(m, a, (unsigned)(v & 0xFFFF));
    wr16(m, a + 2, (unsigned)(v >> 16));
}

static int lfn_refuse(struct dos_m *m, struct dos_regs86 *r)
{
    (void)m;
    r->ax = 0x7100;
    r->flags |= CF;
    return DOS21_DONE;
}

/* The path at seg:off resolved: its volume, drive, and the whole path as UTF-8 in out (2 * DOS_LONG_PATH). */
static int lfn_path(struct dos_m *m, unsigned seg, unsigned off, struct fsv_vol **v, int *drive, char *out)
{
    int e;

    if (str_in(m, seg, off, m->lfn_in, sizeof m->lfn_in) != 0)
        return 0x03;
    if ((e = dos_resolve_long(&m->cds, m->lfn_in, drive, m->lfn_res, sizeof m->lfn_res)) != 0)
        return e;
    if (!(*v = m->vol[*drive]))
        return 0x0F;
    cp_to_utf8(m->cp, m->lfn_res, out, DOS_LONG_PATH * 2);
    return 0;
}

/* A whole path (UTF-8) with each part that is there given its short name (want_long 0) or its long one (1), the
   parts that aren't there as they stand: into out (room bytes, the code page's). 0, or 03h if it doesn't fit. */
static int lfn_names(struct dos_m *m, struct fsv_vol *v, const char *path, int want_long, char *out, unsigned room)
{
    char *pre = m->lfn_p[1];
    char one[FAT_NAME_MAX];
    struct fat_ent fe;
    unsigned at = 0, n = 0, k;

    out[0] = 0;
    while (path[at]) {
        unsigned start;

        while (path[at] == '\\')
            at++;
        if (!path[at])
            break;
        start = at;
        while (path[at] && path[at] != '\\')
            at++;
        if (at >= DOS_LONG_PATH * 2)
            return 0x03;
        memcpy(pre, path, at);
        pre[at] = 0;
        if (fsv_lookup(v, pre, &fe) == 0) {
            if (want_long)
                cp_from_utf8(m->cp, fe.name[0] == '\\' ? fe.name + 1 : fe.name, one, sizeof one);
            else
                strcpy(one, fe.sname[0] == '\\' ? fe.sname + 1 : fe.sname);
        } else {
            char raw[FAT_NAME_MAX];

            if (at - start >= sizeof raw)
                return 0x03;
            memcpy(raw, path + start, at - start);
            raw[at - start] = 0;
            cp_from_utf8(m->cp, raw, one, sizeof one);
        }
        k = (unsigned)strlen(one);
        if (n + 1 + k + 1 > room)
            return 0x03;
        out[n++] = '\\';
        memcpy(out + n, one, k + 1);
        n += k;
    }
    if (!n) {
        if (room < 2)
            return 0x03;
        out[0] = '\\', out[1] = 0;
    }
    return 0;
}

static int lfn_fold(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* A pattern with * and ? against a name. */
static int lfn_glob(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*')
                p++;
            if (!p[1])
                return 1;
            for (; *s; s++)
                if (lfn_glob(p + 1, s))
                    return 1;
            return lfn_glob(p + 1, s);
        }
        if (!*s || (*p != '?' && lfn_fold((unsigned char)*p) != lfn_fold((unsigned char)*s)))
            return 0;
    }
    return !*s;
}

/* A name with no dot is matched as if it ended in one too: "NAME.*" and "*." find it, as under DOS and Windows. */
static int lfn_match1(const char *pat, const char *name)
{
    char dotted[FAT_NAME_MAX + 2];
    unsigned n;

    if (lfn_glob(pat, name))
        return 1;
    if (strchr(name, '.') || (n = (unsigned)strlen(name)) >= FAT_NAME_MAX)
        return 0;
    memcpy(dotted, name, n);
    dotted[n] = '.', dotted[n + 1] = 0;
    return lfn_glob(pat, dotted);
}

static int lfn_match(const char *pat, const struct fat_ent *e)
{
    return !strcmp(pat, "*.*") || !strcmp(pat, "*") || lfn_match1(pat, e->name) || lfn_match1(pat, e->sname);
}

/* A packed date and time as Windows's count of 100 ns since 1601-01-01, at a (8 bytes); or, dos_dates, the time
   and date words and four zero bytes. */
static void lfn_time_out(struct dos_m *m, unsigned long a, unsigned date, unsigned time, int dos_dates)
{
    struct dos_date d;
    struct dos_time t;
    unsigned long long ft;

    if (dos_dates || !date) {
        wr16(m, a, dos_dates ? time : 0), wr16(m, a + 2, dos_dates ? date : 0);
        wr32(m, a + 4, 0);
        return;
    }
    d.year = 1980 + (date >> 9), d.month = (date >> 5) & 15, d.day = date & 31, d.dow = 0;
    t.hour = time >> 11, t.minute = (time >> 5) & 63, t.second = (time & 31) * 2, t.hundredths = 0;
    ft = ((unsigned long long)dos_to_seconds(&d, &t) + 11960006400ULL) * 10000000ULL;
    wr32(m, a, (unsigned long)(ft & 0xFFFFFFFFUL)), wr32(m, a + 4, (unsigned long)(ft >> 32));
}

/* That count made seconds since 1980 (0 before it): a division of 64 bits by hand (the kernel has no library's). */
static unsigned long lfn_ft_seconds(unsigned long lo, unsigned long hi)
{
    unsigned long long n = (unsigned long long)hi << 32 | lo, q = 0, rem = 0;
    int i;

    for (i = 0; i < 64; i++) {                  /* (shifts by one alone: no library call for them either) */
        rem = rem << 1 | n >> 63;
        n <<= 1;
        q <<= 1;
        if (rem >= 10000000ULL)
            rem -= 10000000ULL, q |= 1;
    }
    return q > 11960006400ULL ? (unsigned long)(q - 11960006400ULL) : 0;
}

/* An entry as a search's record at a (318 bytes). */
static void lfn_record(struct dos_m *m, unsigned long a, const struct fat_ent *e, int dos_dates)
{
    char name[FAT_NAME_MAX];
    unsigned i;

    for (i = 0; i < 318; i++)
        wr8(m, a + i, 0);
    wr32(m, a, e->attr);
    lfn_time_out(m, a + 0x04, e->cdate, e->ctime, dos_dates);
    lfn_time_out(m, a + 0x0C, e->adate, 0, dos_dates);
    lfn_time_out(m, a + 0x14, e->wdate, e->wtime, dos_dates);
    wr32(m, a + 0x20, e->size);
    cp_from_utf8(m->cp, e->name, name, sizeof name);
    for (i = 0; name[i] && i < 259; i++)
        wr8(m, a + 0x2C + i, (unsigned char)name[i]);
    if (e->has_long)                            /* (the short name, where the long one isn't it) */
        for (i = 0; e->sname[i] && i < 13; i++)
            wr8(m, a + 0x130 + i, (unsigned char)e->sname[i]);
}

/* The search's next entry that matches into the record: 0, or 12h. */
static int lfn_next(struct dos_m *m, struct dos_lfn_find *f, unsigned long rec)
{
    struct fat_ent e;

    while (fsv_readdir(f->v, &f->dir, &e) == 1) {
        if (!dos_attr_match(f->search, e.attr))
            continue;
        if ((e.attr & f->must) != f->must || !lfn_match(f->pat, &e))
            continue;
        lfn_record(m, rec, &e, f->dos_dates);
        return 0;
    }
    return 0x12;
}

static int lfn(struct dos_m *m, struct dos_regs86 *r, unsigned al)
{
    char *p = m->lfn_p[0], *p2 = m->lfn_p[1];
    struct fsv_vol *v, *v2;
    struct fat_ent fe;
    int drive, drive2, e, d;
    unsigned h, k;

    switch (al) {
    case 0x39:
    case 0x3A:
        if ((e = lfn_path(m, r->ds, r->dx, &v, &drive, p)) != 0)
            return err(m, r, e == 0x02 ? 0x03 : (unsigned)e);
        if (al == 0x3A && !strcmp(p, "\\"))
            return err(m, r, 0x05);
        if (al == 0x3A && lfn_names(m, v, p, 0, m->lfn_res, sizeof m->lfn_res) == 0
            && !strcmp(m->lfn_res, m->cds.cwd[drive]))
            return err(m, r, 0x10);             /* (the current folder) */
        return done(m, r, fs_err(al == 0x39 ? fsv_mkdir(v, p) : fsv_rmdir(v, p), 0x03));
    case 0x3B:
        if ((e = lfn_path(m, r->ds, r->dx, &v, &drive, p)) != 0)
            return err(m, r, e == 0x02 ? 0x03 : (unsigned)e);
        if (strcmp(p, "\\")) {
            if ((e = fsv_lookup(v, p, &fe)) != 0)
                return err(m, r, (unsigned)fs_err(e, 0x03));
            if (!(fe.attr & FAT_A_DIR))
                return err(m, r, 0x03);
        }
        if (lfn_names(m, v, p, 0, m->lfn_res, sizeof m->lfn_res) != 0 || strlen(m->lfn_res) >= sizeof m->cds.cwd[0])
            return err(m, r, 0x03);             /* (the CDS holds the short path: 64 bytes of it) */
        strcpy(m->cds.cwd[drive], m->lfn_res);
        return ok(r);
    case 0x41:
        if ((e = lfn_path(m, r->ds, r->dx, &v, &drive, p)) == 0) {
            if (strchr(p, '*') || strchr(p, '?'))
                e = 0x02;                       /* (patterns, SI 1: not here) */
            else
                e = no_file(v, p, fs_err(fsv_unlink(v, p), 0x02));
        }
        return done(m, r, e);
    case 0x43: {
        unsigned bl = r->bx & 0xFF;

        if ((e = lfn_path(m, r->ds, r->dx, &v, &drive, p)) != 0)
            return err(m, r, (unsigned)e);
        if (bl == 1) {
            if (r->cx & (FAT_A_LABEL | FAT_A_DIR | 0xC0))
                return err(m, r, 0x05);
            return done(m, r, fs_err(fsv_setattr(v, p, (unsigned char)r->cx), 0x02));
        }
        if (bl == 3)
            return done(m, r, fs_err(fsv_settime(v, p, (unsigned short)r->di, (unsigned short)r->cx), 0x02));
        if ((e = fsv_lookup(v, p, &fe)) != 0)
            return err(m, r, (unsigned)no_file(v, p, fs_err(e, 0x02)));
        switch (bl) {
        case 0: r->cx = fe.attr; return ok(r);
        case 2: r->ax = (unsigned)(fe.size & 0xFFFF), r->dx = (unsigned)(fe.size >> 16); return ok(r);
        case 4: r->cx = fe.wtime, r->di = fe.wdate; return ok(r);
        case 5: case 7: return ok(r);           /* (the last access's date, the creation's: kept as they are) */
        case 6: r->di = fe.adate; return ok(r);
        case 8: r->cx = fe.ctime, r->di = fe.cdate, r->si = fe.ctime_10ms; return ok(r);
        }
        return err(m, r, 0x01);
    }
    case 0x47:
        d = (r->dx & 0xFF) ? (int)(r->dx & 0xFF) - 1 : m->cds.current;
        if (d < 0 || d >= DOS_DRIVES || !(v = m->vol[d]))
            return err(m, r, 0x0F);
        cp_to_utf8(m->cp, m->cds.cwd[d], p, DOS_LONG_PATH * 2);
        if (lfn_names(m, v, p, 1, m->lfn_res, sizeof m->lfn_res) != 0)
            return err(m, r, 0x03);
        str_out(m, r->ds, r->si, m->lfn_res + 1);       /* (no drive, no first backslash, as 47h's) */
        return ok(r);
    case 0x4E: {
        struct dos_lfn_find *f = 0;
        char *last;

        if ((e = lfn_path(m, r->ds, r->dx, &v, &drive, p)) != 0)
            return err(m, r, e == 0x02 ? 0x03 : (unsigned)e);
        last = strrchr(p, '\\');                /* (the folder; its last part the pattern) */
        for (k = 0; k < DOS_LFN_FINDS; k++)
            if (!m->lfn[k].used) {
                f = &m->lfn[k];
                break;
            } else if (!f || m->lfn[k].age < f->age)
                f = &m->lfn[k];                 /* (none free: the oldest's) */
        memset(f, 0, sizeof *f);
        if (strlen(last + 1) >= sizeof f->pat)
            return err(m, r, 0x03);
        strcpy(f->pat, last[1] ? last + 1 : "*");
        if (last == p)
            last[1] = 0;                        /* (the root) */
        else
            *last = 0;
        if (fsv_opendir(v, p, &f->dir) != 0)
            return err(m, r, 0x03);
        f->v = v, f->psp = cur_psp(m), f->age = ++m->lfn_age;
        f->search = (unsigned char)(r->cx & 0xFF), f->must = (unsigned char)(r->cx >> 8);
        f->dos_dates = (r->si & 0xFFFF) == 1;
        if ((e = lfn_next(m, f, lin(r->es, r->di))) != 0)
            return err(m, r, (unsigned)e);
        f->used = 1;
        r->ax = (unsigned)(f - m->lfn) + 1;
        r->cx = 0;                              /* (nothing lost in the names' conversion) */
        return ok(r);
    }
    case 0x4F:
    case 0xA1:
        k = (r->bx & 0xFFFF) - 1;
        if (k >= DOS_LFN_FINDS || !m->lfn[k].used)
            return err(m, r, 0x06);
        if (al == 0xA1) {
            m->lfn[k].used = 0;
            return ok(r);
        }
        m->lfn[k].age = ++m->lfn_age;
        if ((e = lfn_next(m, &m->lfn[k], lin(r->es, r->di))) != 0)
            return err(m, r, (unsigned)e);
        r->cx = 0;
        return ok(r);
    case 0x56:
        e = lfn_path(m, r->ds, r->dx, &v, &drive, p);
        if (!e && (e = lfn_path(m, r->es, r->di, &v2, &drive2, p2)) == 0 && drive != drive2)
            e = 0x11;
        if (!e)
            e = no_file(v, p, fs_err(fsv_rename(v, p, p2), 0x02));
        return done(m, r, e);
    case 0x60: {
        unsigned how = r->cx & 0xFF;

        if (how > 2)
            return err(m, r, 0x01);
        if ((e = lfn_path(m, r->ds, r->si, &v, &drive, p)) != 0)
            return err(m, r, (unsigned)e);
        if (how == 0)                           /* (as given, made whole) */
            cp_from_utf8(m->cp, p, m->lfn_res, sizeof m->lfn_res);
        else if (lfn_names(m, v, p, how == 2, m->lfn_res, sizeof m->lfn_res) != 0)
            return err(m, r, 0x03);
        m->lfn_in[0] = (char)('A' + drive), m->lfn_in[1] = ':';
        strcpy(m->lfn_in + 2, m->lfn_res);
        str_out(m, r->es, r->di, m->lfn_in);
        return ok(r);
    }
    case 0x6C: {
        unsigned mode = r->bx & 0xFF, there_act = r->dx & 0x0F, none_act = (r->dx >> 4) & 0x0F, did = 1;
        int there;

        if (str_in(m, r->ds, r->si, m->lfn_in, sizeof m->lfn_in) != 0)
            return err(m, r, 0x03);
        if ((d = dev_name(m->lfn_in)) != 0) {
            if ((e = dos_open(&m->files, cur_psp(m), 0, 0, "", d, mode, &h)) != 0)
                return err(m, r, (unsigned)e);
            r->ax = h, r->cx = 1;
            return ok(r);
        }
        if ((e = lfn_path(m, r->ds, r->si, &v, &drive, p)) != 0)
            return err(m, r, (unsigned)e);
        there = fsv_lookup(v, p, &fe) == 0;
        if (there && (fe.attr & (FAT_A_DIR | FAT_A_LABEL)))
            return err(m, r, 0x05);
        if (there ? there_act == 0 : none_act == 0)
            return err(m, r, there ? 0x50 : (unsigned)no_file(v, p, 0x02));
        if (there_act > 2 || none_act > 1)
            return err(m, r, 0x01);
        if (there && there_act == 1)
            e = dos_open(&m->files, cur_psp(m), v, (unsigned)drive, p, 0, mode, &h);
        else
            e = dos_create(&m->files, cur_psp(m), v, (unsigned)drive, p, (unsigned char)r->cx, there, &h),
            did = there ? 3 : 2;
        if (e)
            return err(m, r, (unsigned)no_file(v, p, e));
        r->ax = h, r->cx = did;
        return ok(r);
    }
    case 0xA0:
        if ((e = lfn_path(m, r->ds, r->dx, &v, &drive, p)) != 0)
            return err(m, r, (unsigned)e);
        if ((r->cx & 0xFFFF) >= 6)
            str_out(m, r->es, r->di, v->fat.fat == 32 ? "FAT32" : "FAT");
        r->bx = 0x4006;                         /* case kept, Unicode on the disk, long names */
        r->cx = 255, r->dx = 260;
        return ok(r);
    case 0xA6: {
        int i = dos_entry(&m->files, cur_psp(m), r->bx);
        unsigned long en, a = lin(r->ds, r->dx);

        if (i < 0)
            return err(m, r, 0x06);
        en = m->files.table + 6 + (unsigned long)i * DOS_SFT_SIZE;
        for (k = 0; k < 52; k++)
            wr8(m, a + k, 0);
        wr32(m, a, rd8(m, en + 0x04));
        lfn_time_out(m, a + 0x04, rd16(m, en + 0x0F), rd16(m, en + 0x0D), 0);
        lfn_time_out(m, a + 0x0C, rd16(m, en + 0x0F), 0, 0);
        lfn_time_out(m, a + 0x14, rd16(m, en + 0x0F), rd16(m, en + 0x0D), 0);
        if (m->files.k[i].f.v)
            wr32(m, a + 0x1C, m->files.k[i].f.v->fat.serial);
        wr32(m, a + 0x24, rd16(m, en + 0x11) | (unsigned long)rd16(m, en + 0x13) << 16);
        wr32(m, a + 0x28, 1);                   /* (one name for it) */
        wr32(m, a + 0x30, (unsigned long)i + 1);
        return ok(r);
    }
    case 0xA7:
        if ((r->bx & 0xFF) == 0) {              /* Windows's count at DS:SI to DOS's words */
            struct dos_date dd;
            struct dos_time tt;
            unsigned long a = lin(r->ds, r->si);
            unsigned long secs = lfn_ft_seconds(rd16(m, a) | (unsigned long)rd16(m, a + 2) << 16,
                                                rd16(m, a + 4) | (unsigned long)rd16(m, a + 6) << 16);

            dos_from_seconds(secs, &dd, &tt);
            r->cx = dos_pack_time(&tt), r->dx = dos_pack_date(&dd);
            r->bx &= 0x00FF;
            return ok(r);
        }
        if ((r->bx & 0xFF) == 1) {
            lfn_time_out(m, lin(r->es, r->di), r->dx, r->cx, 0);
            return ok(r);
        }
        return err(m, r, 0x01);
    }
    m->unanswered++;
    return lfn_refuse(m, r);
}

/* ---- INT 21h */

static int dos21_call(struct dos_m *m, struct dos_regs86 *r)
{
    unsigned ah = ah_of(r), al = al_of(r), h;
    char path[128], path2[128];
    struct fsv_vol *v, *v2;
    struct fat_ent fe;
    int drive, drive2, e, d;

    if (m->fill)
        m->fill(m->ctx, ah == 0x01 || ah == 0x07 || ah == 0x08 || ah == 0x0A || ah == 0x0B || ah == 0x0C
                            || (ah == 0x06 && (r->dx & 0xFF) == 0xFF));
    if (m->ctrl_break && ah != 0x06 && ah != 0x07 && ah != 0x33 && ah != 0x4C && break_waiting(m))
        return DOS21_CTRLC;                     /* (BREAK ON: every call looks) */
    switch (ah) {
    case 0x00:
        return end_program(m, r, 0, 0);
    case 0x01:
    case 0x07:
    case 0x08:
        if ((e = getc_dos(m, ah != 0x07, ah == 0x01)) == -1)
            return DOS21_GONE;
        if (e == -2)
            return DOS21_CTRLC;
        set_al(r, (unsigned)e);
        return DOS21_DONE;
    case 0x02:
    case 0x06:
        if (ah == 0x06 && (r->dx & 0xFF) == 0xFF) {
            int k = m->pend_scan ? 0 : dos_key_peek(m);

            if (k < 0) {                        /* (nothing typed: ZF, and the machine's time given up) */
                r->flags |= ZF;
                set_al(r, 0);
                if (m->idle)
                    m->idle(m->ctx);
                return DOS21_DONE;
            }
            r->flags &= ~ZF;
            set_al(r, (unsigned)getc_dos(m, 0, 0));
            return DOS21_DONE;
        }
        if (ah == 0x02 && break_waiting(m))
            return DOS21_CTRLC;
        {
            char c = (char)(r->dx & 0xFF);

            con_out(m, &c, 1);
            set_al(r, (unsigned char)c);
        }
        return DOS21_DONE;
    case 0x09: {
        unsigned long a = lin(r->ds, r->dx), i;

        if (break_waiting(m))
            return DOS21_CTRLC;
        for (i = 0; i < 65536 && a + i < m->size && m->mem[a + i] != '$'; i++)
            ;
        if (a < m->size)
            con_out(m, (const char *)m->mem + a, (unsigned)i);
        set_al(r, '$');
        return DOS21_DONE;
    }
    case 0x0A: {
        unsigned long b = lin(r->ds, r->dx);
        unsigned max = rd8(m, b);

        if (b + 2 + max > m->size)
            return DOS21_DONE;
        if ((e = read_line(m, m->mem + b + 2, max)) == -1)
            return DOS21_GONE;
        return e == -2 ? DOS21_CTRLC : DOS21_DONE;
    }
    case 0x0B:
        if (break_waiting(m))
            return DOS21_CTRLC;
        set_al(r, dos_key_peek(m) >= 0 || m->pend_scan ? 0xFF : 0);
        if (!(r->ax & 0xFF) && m->idle)
            m->idle(m->ctx);
        return DOS21_DONE;
    case 0x0C:
        m->ktail = m->khead;                    /* (what was typed ahead, gone) */
        m->pend_scan = 0;
        m->line_n = m->line_at = 0;
        if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08 || al == 0x0A) {
            r->ax = al << 8 | (r->ax & 0xFF);
            return dos21_call(m, r);
        }
        set_al(r, 0);
        return DOS21_DONE;
    case 0x0D:
        for (d = 0; d < DOS_DRIVES; d++)
            if (m->vol[d])
                fsv_flush(m->vol[d]);
        return DOS21_DONE;
    case 0x0E:
        if ((r->dx & 0xFF) < DOS_DRIVES && (m->cds.valid >> (r->dx & 0xFF) & 1)) {
            m->cds.current = (int)(r->dx & 0xFF);
            wr8(m, m->lay.sda + 0x16, (unsigned)m->cds.current);
        }
        set_al(r, DOS_DRIVES);                  /* (MS-DOS 7.1's: 26, LASTDRIVE=Z) */
        return DOS21_DONE;
    case 0x19:
        set_al(r, (unsigned)m->cds.current);
        return DOS21_DONE;
    case 0x1A:
        set_dta(m, r->ds, r->dx);
        return DOS21_DONE;
    case 0x25:
        set_vec(m, al, (unsigned long)r->ds << 16 | r->dx);
        return DOS21_DONE;
    case 0x29:
        return parse_fcb(m, r, al);
    case 0x2A:
    case 0x2C: {
        struct dos_date dd;
        struct dos_time tt;

        dos_get(m->now ? m->now(m->ctx) : 0, 0, m->time_offset, &dd, &tt);
        if (ah == 0x2A) {
            r->cx = dd.year, r->dx = dd.month << 8 | dd.day;
            set_al(r, dd.dow);
        } else {
            r->cx = tt.hour << 8 | tt.minute, r->dx = tt.second << 8 | tt.hundredths;
        }
        return DOS21_DONE;
    }
    case 0x2B:
        set_al(r, (unsigned)dos_set_date(m->now ? m->now(m->ctx) : 0, &m->time_offset, r->cx, r->dx >> 8,
                                         r->dx & 0xFF));
        return DOS21_DONE;
    case 0x2D:
        set_al(r, (unsigned)dos_set_time(m->now ? m->now(m->ctx) : 0, &m->time_offset, r->cx >> 8, r->cx & 0xFF,
                                         r->dx >> 8, r->dx & 0xFF));
        return DOS21_DONE;
    case 0x2E:
        m->verify = (unsigned char)(al != 0);
        return DOS21_DONE;
    case 0x2F:
        r->es = dta_seg(m), r->bx = dta_off(m);
        return DOS21_DONE;
    case 0x30:
        r->ax = 0x0A07;                         /* (7.10) */
        r->bx = al == 1 ? 0x0000 : 0xFF00;      /* (OEM FFh, Microsoft; or neither in the HMA nor in ROM) */
        r->cx = 0;
        return DOS21_DONE;
    case 0x33:
        switch (al) {
        case 0x00:
            r->dx = (r->dx & 0xFF00) | m->ctrl_break;
            return DOS21_DONE;
        case 0x01:
            m->ctrl_break = (unsigned char)((r->dx & 0xFF) != 0);
            return DOS21_DONE;
        case 0x05:
            r->dx = (r->dx & 0xFF00) | 3;       /* (it started from C:) */
            return DOS21_DONE;
        case 0x06:
            r->bx = 0x0A07, r->dx = 0;          /* (7.10, revision 0) */
            return DOS21_DONE;
        }
        break;
    case 0x31:
        return end_program(m, r, al, 3);
    case 0x34:
        r->es = (unsigned)(m->lay.sda >> 4), r->bx = (unsigned)(m->lay.sda & 0xF) + 1;
        return DOS21_DONE;
    case 0x35: {
        unsigned long x = vec(m, al);

        r->bx = (unsigned)x & 0xFFFF, r->es = (unsigned)(x >> 16) & 0xFFFF;
        return DOS21_DONE;
    }
    case 0x36: {
        unsigned long fr;

        d = (r->dx & 0xFF) ? (int)(r->dx & 0xFF) - 1 : m->cds.current;
        if (d < 0 || d >= DOS_DRIVES || !(v = m->vol[d]) || fsv_free(v, &fr) != 0) {
            r->ax = 0xFFFF;
            return DOS21_DONE;
        }
        r->ax = v->fat.spc;                     /* (FAT32's large counts: to check how 7.1 scales them) */
        r->bx = (unsigned)(fr > 0xFFFF ? 0xFFFF : fr);
        r->cx = 512;
        r->dx = (unsigned)(v->fat.clusters > 0xFFFF ? 0xFFFF : v->fat.clusters);
        return DOS21_DONE;
    }
    case 0x38:
        if (al == 0 || al == 1)
            return country(m, r);
        break;
    case 0x39:
    case 0x3A:
        if ((e = resolve(m, r->ds, r->dx, &v, &drive, path)) != 0)
            return err(m, r, e == 0x02 ? 0x03 : (unsigned)e);
        if (ah == 0x3A && !strcmp(path, "\\"))
            return err(m, r, 0x05);             /* (the root: MS-DOS 7.1's 05h) */
        if (ah == 0x3A && !strcmp(path, m->cds.cwd[drive]))
            return err(m, r, 0x10);             /* (the current folder) */
        return done(m, r, fs_err(ah == 0x39 ? fsv_mkdir(v, path) : fsv_rmdir(v, path), 0x03));
    case 0x3B:
        if ((e = resolve(m, r->ds, r->dx, &v, &drive, path)) != 0)
            return err(m, r, e == 0x02 ? 0x03 : (unsigned)e);
        if (strcmp(path, "\\")) {
            if ((e = fsv_lookup(v, path, &fe)) != 0)
                return err(m, r, (unsigned)fs_err(e, 0x03));
            if (!(fe.attr & FAT_A_DIR))
                return err(m, r, 0x03);
        }
        if (strlen(path) >= sizeof m->cds.cwd[0])
            return err(m, r, 0x03);
        strcpy(m->cds.cwd[drive], path);
        return ok(r);
    case 0x3C:
    case 0x5B:
    case 0x3D:
        if (str_in(m, r->ds, r->dx, path2, sizeof path2) != 0)
            return err(m, r, 0x03);
        if ((d = dev_name(path2)) != 0)
            e = dos_open(&m->files, cur_psp(m), 0, 0, "", d, ah == 0x3D ? al : 2, &h);
        else if ((e = resolve_str(m, path2, &v, &drive, path)) != 0)
            ;
        else if (ah == 0x3D)
            e = no_file(v, path, dos_open(&m->files, cur_psp(m), v, (unsigned)drive, path, 0, al, &h));
        else
            e = no_file(v, path, dos_create(&m->files, cur_psp(m), v, (unsigned)drive, path, (unsigned char)r->cx,
                                            ah == 0x3C, &h));
        if (e)
            return err(m, r, (unsigned)e);
        r->ax = h;
        return ok(r);
    case 0x3E:
        return done(m, r, dos_close(&m->files, cur_psp(m), r->bx));
    case 0x3F:
    case 0x40:
        return file_io(m, r, ah);
    case 0x41:
        if ((e = resolve(m, r->ds, r->dx, &v, &drive, path)) == 0)
            e = no_file(v, path, fs_err(fsv_unlink(v, path), 0x02));
        return done(m, r, e);
    case 0x42: {
        unsigned long pos;
        long off = (long)((unsigned long)r->cx << 16 | (r->dx & 0xFFFF));

        if ((e = dos_seek(&m->files, cur_psp(m), r->bx, (int)al, off, &pos)) != 0)
            return err(m, r, (unsigned)e);
        r->ax = (unsigned)(pos & 0xFFFF), r->dx = (unsigned)(pos >> 16) & 0xFFFF;
        return ok(r);
    }
    case 0x43:
        if (al > 1)
            break;
        if ((e = resolve(m, r->ds, r->dx, &v, &drive, path)) != 0)
            return err(m, r, (unsigned)e);
        if (al == 0) {
            if ((e = fsv_lookup(v, path, &fe)) != 0)
                return err(m, r, (unsigned)no_file(v, path, fs_err(e, 0x02)));
            r->cx = fe.attr;
            return ok(r);
        }
        if (r->cx & (FAT_A_LABEL | FAT_A_DIR | 0xC0))
            return err(m, r, 0x05);
        return done(m, r, fs_err(fsv_setattr(v, path, (unsigned char)r->cx), 0x02));
    case 0x44:
        if ((e = ioctl(m, r, al)) != DOS21_UNANSWERED)
            return e;
        break;
    case 0x45:
        if ((e = dos_dup(&m->files, cur_psp(m), r->bx, &h)) != 0)
            return err(m, r, (unsigned)e);
        r->ax = h;
        return ok(r);
    case 0x46:
        return done(m, r, dos_dup2(&m->files, cur_psp(m), r->bx, r->cx));
    case 0x47:
        if ((e = dos_getcwd(&m->cds, (int)(r->dx & 0xFF), path, 64)) != 0)
            return err(m, r, (unsigned)e);
        str_out(m, r->ds, r->si, path);
        r->ax = 0x0100;                         /* (as DOS leaves AX: to check) */
        return ok(r);
    case 0x48: {
        unsigned seg, largest;

        if ((e = dos_mem_alloc(&m->mcb, r->bx, cur_psp(m), &seg, &largest)) != 0) {
            r->bx = largest;
            return err(m, r, (unsigned)e);
        }
        r->ax = seg;
        return ok(r);
    }
    case 0x49:
        return done(m, r, dos_mem_free(&m->mcb, r->es));
    case 0x4A: {
        unsigned largest;

        if ((e = dos_mem_resize(&m->mcb, r->es, r->bx, &largest)) != 0) {
            r->bx = largest;
            return err(m, r, (unsigned)e);
        }
        return ok(r);
    }
    case 0x4B:
        if (al == 0x00 || al == 0x01 || al == 0x03)
            return exec_call(m, r, al);
        break;
    case 0x4C:
        return end_program(m, r, al, 0);
    case 0x4D:
        r->ax = m->exit_code;                   /* (read once: then 0, as DOS 5+ do) */
        m->exit_code = 0;
        return ok(r);
    case 0x4E:
    case 0x4F:
        return find(m, r, ah);
    case 0x50:
        set_psp(m, r->bx);
        return DOS21_DONE;
    case 0x51:
    case 0x62:
        r->bx = cur_psp(m);
        return DOS21_DONE;
    case 0x52:
        r->es = (unsigned)(m->lay.lol >> 4), r->bx = (unsigned)(m->lay.lol & 0xF);
        return DOS21_DONE;
    case 0x54:
        set_al(r, m->verify);
        return DOS21_DONE;
    case 0x5D:
        if (al == 0x06) {                       /* the swappable data area, all of it always */
            r->ds = (unsigned)(m->lay.sda >> 4), r->si = (unsigned)(m->lay.sda & 0xF);
            r->cx = DOS_SDA_SIZE, r->dx = DOS_SDA_SIZE;
            return ok(r);
        }
        break;
    case 0x60:
        if (str_in(m, r->ds, r->si, path2, sizeof path2) != 0)
            return err(m, r, 0x03);
        if (dev_name(path2) && !strchr(path2, '\\') && !strchr(path2, '/') && !strchr(path2, ':')) {
            unsigned k, b;                      /* (a device's bare name: "C:/NUL", as MS-DOS 7.1 gives it) */

            path[0] = (char)('A' + m->cds.current), path[1] = ':', path[2] = '/';
            for (k = 0, b = 3; path2[k] && path2[k] != '.' && b < 12; k++)
                path[b++] = (char)dos_upper(m->cp, (unsigned char)path2[k]);
            path[b] = 0;
        } else if ((e = dos_truename(&m->cds, m->cp, path2, path, sizeof path)) != 0)
            return err(m, r, (unsigned)e);
        str_out(m, r->es, r->di, path);
        return ok(r);
    case 0x68:
    case 0x6A:
        if (dos_entry(&m->files, cur_psp(m), r->bx) < 0)
            return err(m, r, 0x06);
        for (d = 0; d < DOS_DRIVES; d++)        /* (writes go through: the volume's own buffers flushed) */
            if (m->vol[d])
                fsv_flush(m->vol[d]);
        return ok(r);
    case 0x69:
        if (al == 0x00)
            return serial(m, r);
        break;
    case 0x6C:
        if (al == 0)
            return ext_open(m, r);
        break;
    case 0x73:
        if (al == 0x03)
            return free32(m, r);
        if (al == 0x02)
            return ext_dpb(m, r);
        break;
    case 0x56:
        e = resolve(m, r->ds, r->dx, &v, &drive, path);
        if (!e && (e = resolve(m, r->es, r->di, &v2, &drive2, path2)) == 0 && drive != drive2)
            e = 0x11;                           /* (not the same device) */
        if (!e)
            e = no_file(v, path, fs_err(fsv_rename(v, path, path2), 0x02));
        return done(m, r, e);
    case 0x57:
        if (al == 0) {
            int i = dos_entry(&m->files, cur_psp(m), r->bx);
            unsigned long en;

            if (i < 0)
                return err(m, r, 0x06);
            en = m->files.table + 6 + (unsigned long)i * DOS_SFT_SIZE;
            r->cx = rd16(m, en + 0x0D), r->dx = rd16(m, en + 0x0F);
            return ok(r);
        }
        break;
    case 0x58:
        switch (al) {
        case 0x00:
            r->ax = m->mcb.strategy;
            return ok(r);
        case 0x01:
            return done(m, r, dos_mem_strategy(&m->mcb, r->bx & 0xFF));
        case 0x02:
            set_al(r, (unsigned)m->mcb.linked);
            return ok(r);
        case 0x03:
            return done(m, r, dos_mem_link(&m->mcb, r->bx != 0));
        }
        break;
    case 0x59: {
        unsigned c = m->last_error;

        /* class, action and locus as MS-DOS 7.1 gives them (DOSDIFF: 01h, 02h, 03h, 05h, 06h, 09h, 0Ch, 10h,
           12h, 50h); 04h, 08h and 20h as RBIL's tables have them, to check; anything else 07h, 04h, 01h */
        unsigned cls = 7, act = 4, loc = 1;

        if (c == 2 || c == 3 || c == 0x12)
            cls = 8, act = 3, loc = 2;
        else if (c == 5 || c == 0x10)
            cls = 3, act = 3, loc = 2;
        else if (c == 0x50)
            cls = 0x0C, act = 3, loc = 2;
        else if (c == 9)
            loc = 5;
        else if (c == 4 || c == 8)
            cls = 1, act = 4, loc = c == 8 ? 5 : 1;
        else if (c == 0x20)
            cls = 0x0A, act = 2, loc = 2;
        r->ax = c;
        r->bx = cls << 8 | act;
        r->cx = (r->cx & 0x00FF) | loc << 8;
        return DOS21_DONE;
    }
    case 0x63:
        if (al == 0) {
            r->ds = (unsigned)(m->lay.misc >> 4), r->si = (unsigned)(m->lay.misc & 0xF);
            set_al(r, 0);
            return ok(r);
        }
        break;
    case 0x66:
        if (al == 1) {
            r->bx = (unsigned)m->cp, r->dx = (unsigned)m->cp;
            return ok(r);
        }
        break;
    case 0x71:                                  /* (long names: MS-DOS 7.1 with no Windows refuses them all) */
        return lfn(m, r, al);
    }
    m->unanswered++;
    if (ah >= 0x2F) {                           /* (MS-DOS 7.1's way with a function it hasn't got: to check) */
        r->ax = 0x0001;
        r->flags |= CF;
    } else {
        set_al(r, 0);
    }
    return DOS21_UNANSWERED;
}

int dos21(struct dos_m *m, struct dos_regs86 *r)
{
    struct dos_regs86 in = *r;
    int res = dos21_call(m, r);

    if (res == DOS21_CTRLC || res == DOS21_GONE)
        *r = in;                                /* (as it came: INT 23h's, and the call made again after it) */
    return res;
}

int dos_ctrl_c_end(struct dos_m *m, struct dos_regs86 *r)
{
    return end_program(m, r, 0, 1);
}
