/* The V86 monitor's instruction decoder (supervisor.md §9.2). It sees only
 * the bytes at CS:IP, so tests/host/v86dec_test.c runs it on the host. */
#ifndef K_V86DEC_H
#define K_V86DEC_H
#include "types.h"

enum v86_kind {
    V86_OTHER,                  /* not one the monitor emulates: a real fault */
    V86_CLI, V86_STI, V86_PUSHF, V86_POPF, V86_INT, V86_IRET, V86_HLT,
    V86_IN, V86_OUT, V86_INS, V86_OUTS,
    V86_PRIV,                   /* 0Fh xx: a system instruction (MOV CRn, LMSW, ...) */
};

#define V86_SEG_NONE 0xFF       /* else 0 ES, 1 CS, 2 SS, 3 DS, 4 FS, 5 GS */
#define V86_MAX_LEN  15

struct v86insn {
    u8 kind;
    u8 len;                     /* bytes, prefixes included */
    u8 opsize;                  /* 2, or 4 with 66h */
    u8 adsize;                  /* 2, or 4 with 67h */
    u8 seg;                     /* segment override */
    u8 rep;                     /* F2h or F3h if present, else 0 */
    u8 width;                   /* IN, OUT, INS, OUTS: 1, 2 or 4 */
    u8 imm;                     /* INT n: n; IN/OUT imm8: the port; PRIV: the second opcode byte */
    u8 port_dx;                 /* IN, OUT: the port is in DX */
};

/* Decodes the instruction in b[0..n-1] (n is V86_MAX_LEN at CS:IP); returns in->kind. */
int v86_decode(const u8 *b, u32 n, struct v86insn *in);
/* The same with 2- or 4-byte default operand and address sizes (protected-mode code). */
int v86_decode_size(const u8 *b, u32 n, struct v86insn *in, u32 def);

#endif
