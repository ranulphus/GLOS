/* The V86 monitor's decoder (supervisor.md §9.2): the instructions that fault
 * in virtual-8086 mode at IOPL 0, after any prefixes. INT3 and INTO are not
 * here: they are not IOPL-sensitive in V86 mode and arrive as #BP and #OF. */
#include "v86dec.h"

int v86_decode(const u8 *b, u32 n, struct v86insn *in)
{
    u32 i = 0;
    u8 op;

    in->kind = V86_OTHER;
    in->len = 0;
    in->opsize = 2;
    in->adsize = 2;
    in->seg = V86_SEG_NONE;
    in->rep = 0;
    in->width = 0;
    in->imm = 0;
    in->port_dx = 0;
    if (n > V86_MAX_LEN)
        n = V86_MAX_LEN;

    for (;; i++) {
        if (i >= n) {                           /* nothing but prefixes: too long */
            in->len = (u8)i;
            return V86_OTHER;
        }
        switch (b[i]) {
        case 0x66: in->opsize = 4; continue;
        case 0x67: in->adsize = 4; continue;
        case 0x26: in->seg = 0; continue;
        case 0x2E: in->seg = 1; continue;
        case 0x36: in->seg = 2; continue;
        case 0x3E: in->seg = 3; continue;
        case 0x64: in->seg = 4; continue;
        case 0x65: in->seg = 5; continue;
        case 0xF2: case 0xF3: in->rep = b[i]; continue;
        case 0xF0: continue;
        }
        break;
    }

    op = b[i++];
    switch (op) {
    case 0xFA: in->kind = V86_CLI; break;
    case 0xFB: in->kind = V86_STI; break;
    case 0x9C: in->kind = V86_PUSHF; break;
    case 0x9D: in->kind = V86_POPF; break;
    case 0xCF: in->kind = V86_IRET; break;
    case 0xF4: in->kind = V86_HLT; break;
    case 0xCD:
    case 0xE4: case 0xE5: case 0xE6: case 0xE7:
    case 0x0F:
        if (i >= n) {
            in->len = (u8)i;
            return V86_OTHER;
        }
        in->imm = b[i++];
        if (op == 0xCD) {
            in->kind = V86_INT;
        } else if (op == 0x0F) {
            in->kind = V86_PRIV;
        } else {
            in->kind = (op & 2) ? V86_OUT : V86_IN;
            in->width = (op & 1) ? in->opsize : 1;
        }
        break;
    case 0xEC: case 0xED: case 0xEE: case 0xEF:
        in->kind = (op & 2) ? V86_OUT : V86_IN;
        in->width = (op & 1) ? in->opsize : 1;
        in->port_dx = 1;
        break;
    case 0x6C: case 0x6D: case 0x6E: case 0x6F:
        in->kind = (op & 2) ? V86_OUTS : V86_INS;
        in->width = (op & 1) ? in->opsize : 1;
        in->port_dx = 1;
        break;
    default:
        break;
    }
    in->len = (u8)i;
    return in->kind;
}
