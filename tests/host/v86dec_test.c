/* Host test for the V86 decoder (kernel/vm/v86dec.c): every instruction the
 * monitor emulates, under each prefix it accepts, plus the cases it must
 * leave alone. */
#include <stdio.h>
#include <string.h>
#include "v86dec.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct row {
    const char *name;
    u8 bytes[6];
    u8 n;
    u8 kind, len, width, imm, port_dx;
};

static const struct row rows[] = {
    { "cli",       { 0xFA }, 1, V86_CLI, 1, 0, 0, 0 },
    { "sti",       { 0xFB }, 1, V86_STI, 1, 0, 0, 0 },
    { "pushf",     { 0x9C }, 1, V86_PUSHF, 1, 0, 0, 0 },
    { "popf",      { 0x9D }, 1, V86_POPF, 1, 0, 0, 0 },
    { "iret",      { 0xCF }, 1, V86_IRET, 1, 0, 0, 0 },
    { "hlt",       { 0xF4 }, 1, V86_HLT, 1, 0, 0, 0 },
    { "int 21h",   { 0xCD, 0x21 }, 2, V86_INT, 2, 0, 0x21, 0 },
    { "in al,60h", { 0xE4, 0x60 }, 2, V86_IN, 2, 1, 0x60, 0 },
    { "in ax,40h", { 0xE5, 0x40 }, 2, V86_IN, 2, 2, 0x40, 0 },
    { "out 20h,al", { 0xE6, 0x20 }, 2, V86_OUT, 2, 1, 0x20, 0 },
    { "out 92h,ax", { 0xE7, 0x92 }, 2, V86_OUT, 2, 2, 0x92, 0 },
    { "in al,dx",  { 0xEC }, 1, V86_IN, 1, 1, 0, 1 },
    { "in ax,dx",  { 0xED }, 1, V86_IN, 1, 2, 0, 1 },
    { "out dx,al", { 0xEE }, 1, V86_OUT, 1, 1, 0, 1 },
    { "out dx,ax", { 0xEF }, 1, V86_OUT, 1, 2, 0, 1 },
    { "insb",      { 0x6C }, 1, V86_INS, 1, 1, 0, 1 },
    { "insw",      { 0x6D }, 1, V86_INS, 1, 2, 0, 1 },
    { "outsb",     { 0x6E }, 1, V86_OUTS, 1, 1, 0, 1 },
    { "outsw",     { 0x6F }, 1, V86_OUTS, 1, 2, 0, 1 },
    { "mov eax,cr0", { 0x0F, 0x20, 0xC0 }, 3, V86_PRIV, 2, 0, 0x20, 0 },
    { "lmsw",      { 0x0F, 0x01, 0xF0 }, 3, V86_PRIV, 2, 0, 0x01, 0 },
    { "nop",       { 0x90 }, 1, V86_OTHER, 1, 0, 0, 0 },
    { "mov ax,bx", { 0x89, 0xD8 }, 2, V86_OTHER, 1, 0, 0, 0 },
};

int main(void)
{
    struct v86insn in;
    u8 b[V86_MAX_LEN + 4];
    unsigned i;

    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const struct row *r = &rows[i];
        int k = v86_decode(r->bytes, r->n, &in);
        if (k != r->kind || in.len != r->len || in.width != r->width || in.imm != r->imm
            || in.port_dx != r->port_dx || in.opsize != 2 || in.adsize != 2 || in.seg != V86_SEG_NONE) {
            printf("FAIL %s: kind=%d len=%d width=%d imm=%x dx=%d\n", r->name, k, in.len, in.width, in.imm,
                   in.port_dx);
            fails++;
        }
    }

    /* 66h: PUSHFD/POPFD/IRETD and 32-bit I/O. */
    memcpy(b, "\x66\x9C", 2);
    CHECK(v86_decode(b, 2, &in) == V86_PUSHF && in.opsize == 4 && in.len == 2);
    memcpy(b, "\x66\x9D", 2);
    CHECK(v86_decode(b, 2, &in) == V86_POPF && in.opsize == 4);
    memcpy(b, "\x66\xCF", 2);
    CHECK(v86_decode(b, 2, &in) == V86_IRET && in.opsize == 4);
    memcpy(b, "\x66\xED", 2);
    CHECK(v86_decode(b, 2, &in) == V86_IN && in.width == 4 && in.port_dx);
    memcpy(b, "\x66\xE7\xCC", 3);
    CHECK(v86_decode(b, 3, &in) == V86_OUT && in.width == 4 && in.imm == 0xCC && in.len == 3);
    memcpy(b, "\x66\xE6\x80", 3);                       /* 66h does not widen a byte access */
    CHECK(v86_decode(b, 3, &in) == V86_OUT && in.width == 1);

    /* REP, segment overrides and 67h on string I/O. */
    memcpy(b, "\xF3\x26\x6C", 3);
    CHECK(v86_decode(b, 3, &in) == V86_INS && in.rep == 0xF3 && in.seg == 0 && in.len == 3);
    memcpy(b, "\x2E\xF3\x6E", 3);
    CHECK(v86_decode(b, 3, &in) == V86_OUTS && in.rep == 0xF3 && in.seg == 1 && in.width == 1);
    memcpy(b, "\x67\x66\xF3\x6F", 4);
    CHECK(v86_decode(b, 4, &in) == V86_OUTS && in.adsize == 4 && in.opsize == 4 && in.width == 4 && in.len == 4);
    memcpy(b, "\x64\x6D", 2);
    CHECK(v86_decode(b, 2, &in) == V86_INS && in.seg == 4);
    memcpy(b, "\x65\x36\x6D", 3);                       /* the last override wins */
    CHECK(v86_decode(b, 3, &in) == V86_INS && in.seg == 2);
    memcpy(b, "\x3E\xF0\xFA", 3);                       /* prefixes before CLI are skipped */
    CHECK(v86_decode(b, 3, &in) == V86_CLI && in.len == 3);

    /* Truncated: an operand byte missing, or nothing but prefixes. */
    CHECK(v86_decode((const u8 *)"\xCD", 1, &in) == V86_OTHER);
    CHECK(v86_decode((const u8 *)"\xE4", 1, &in) == V86_OTHER);
    memset(b, 0x66, sizeof b);
    CHECK(v86_decode(b, sizeof b, &in) == V86_OTHER && in.len == V86_MAX_LEN);
    memset(b, 0x66, V86_MAX_LEN - 1);
    b[V86_MAX_LEN - 1] = 0xFA;                          /* 14 prefixes then CLI: still 15 bytes */
    CHECK(v86_decode(b, sizeof b, &in) == V86_CLI && in.len == V86_MAX_LEN);

    printf("v86dec_test: %d failures\n", fails);
    return fails != 0;
}
