/* A program's PSP and environment (psp.h). */
#include <string.h>

#include "psp.h"

static void wr16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void wr32(unsigned char *p, unsigned long v)
{
    wr16(p, (unsigned)(v & 0xFFFF));
    wr16(p + 2, (unsigned)(v >> 16));
}

static void blank_fcb(unsigned char *f)
{
    memset(f, 0, 16);
    memset(f + 1, ' ', 11);
}

void dos_psp_make(unsigned char *p, const struct dos_psp_init *in)
{
    unsigned long n = strlen(in->tail);

    memset(p, 0, 256);
    p[0] = 0xCD;                                /* INT 20h */
    p[1] = 0x20;
    wr16(p + 0x02, in->top);
    p[0x05] = 0x9A;                             /* CALL FAR F01D:FEF0, which wraps to 0000:00C0 (CP/M's CALL 5); */
    wr16(p + 0x06, 0xFEF0);                     /* FEF0h is also the "segment's size" CP/M programs read */
    wr16(p + 0x08, 0xF01D);
    wr32(p + 0x0A, in->int22);
    wr32(p + 0x0E, in->int23);
    wr32(p + 0x12, in->int24);
    wr16(p + 0x16, in->parent);
    if (in->jft)
        memcpy(p + 0x18, in->jft, 20);
    else
        memset(p + 0x18, 0xFF, 20);
    wr16(p + 0x2C, in->env);
    wr16(p + 0x32, 20);                         /* its handle table: 20, here */
    wr16(p + 0x34, 0x18);
    wr16(p + 0x36, in->seg);
    wr32(p + 0x38, 0xFFFFFFFFUL);
    wr16(p + 0x40, in->version);
    p[0x50] = 0xCD;                             /* INT 21h; RETF */
    p[0x51] = 0x21;
    p[0x52] = 0xCB;
    if (in->fcb1)
        memcpy(p + 0x5C, in->fcb1, 16);
    else
        blank_fcb(p + 0x5C);
    if (in->fcb2)
        memcpy(p + 0x6C, in->fcb2, 16);
    else
        blank_fcb(p + 0x6C);
    /* A tail longer than DOS passes: its first 126 characters, and the length 127 (Windows 9x's sign that the whole
       line is in the environment's CMDLINE, where its caller put it: a DJGPP program looks there). MS-DOS 7 copies
       the length byte as it is given, so EXEC passes such a 127 on. */
    p[0x80] = (unsigned char)(n > 126 ? 127 : n);
    if (n > 126)
        n = 126;
    memcpy(p + 0x81, in->tail, n);
    p[0x81 + n] = 0x0D;
}

unsigned long dos_env_make(unsigned char *dst, unsigned long room, const unsigned char *env, unsigned long env_max,
                           const char *program)
{
    unsigned long n = 0, pl = strlen(program) + 1;

    /* The parent's strings, up to the empty one that ends them (an empty environment: just that). */
    if (env) {
        while (n + 1 < env_max && !(env[n] == 0 && (n == 0 || env[n - 1] == 0)))
            n++;
        if (n + 1 >= env_max)
            return 0;                           /* (no end found: not an environment) */
    }
    n++;                                        /* (the empty string's NUL) */
    if (n == 1) {                               /* (none: two NULs, as DOS gives an empty one) */
        n = 2;
        if (room < n + 2 + pl)
            return 0;
        dst[0] = dst[1] = 0;
    } else {
        if (room < n + 2 + pl)
            return 0;
        memcpy(dst, env, n);
    }
    if (n + 2 + pl > 32768UL)
        return 0;
    wr16(dst + n, 1);
    memcpy(dst + n + 2, program, pl);
    return n + 2 + pl;
}
