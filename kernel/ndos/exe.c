/* Programs loaded by GLOS's own DOS (exe.h). */
#include "exe.h"

static unsigned rd16(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

int dos_exe_header(const unsigned char *h, unsigned long head_len, unsigned long file_size, struct dos_exe *x)
{
    unsigned last, pages, hdr;
    unsigned long total;

    x->exe = 0;
    x->image_off = 0;
    x->image_len = file_size;
    x->image_paras = (file_size + 15) / 16;
    x->min_alloc = x->max_alloc = 0;
    x->ss = x->sp = x->cs = x->ip = 0;
    x->nrel = 0;
    x->rel_off = 0;
    x->high = 0;
    if (head_len < 2 || !((h[0] == 'M' && h[1] == 'Z') || (h[0] == 'Z' && h[1] == 'M')))
        return 0;                       /* (a .COM: the whole file) */
    if (head_len < 28)
        return -1;
    last = rd16(h + 2);
    pages = rd16(h + 4);
    hdr = rd16(h + 8);
    total = (unsigned long)pages * 512;
    if (last && pages)
        total -= 512 - (last & 511 ? last & 511 : 512);
    if (total < (unsigned long)hdr * 16)
        return -1;
    x->exe = 1;
    x->image_off = (unsigned long)hdr * 16;
    x->image_len = total - x->image_off;
    x->image_paras = (x->image_len + 15) / 16;
    x->nrel = rd16(h + 6);
    x->min_alloc = rd16(h + 10);
    x->max_alloc = rd16(h + 12);
    x->ss = rd16(h + 14);
    x->sp = rd16(h + 16);
    x->ip = rd16(h + 20);
    x->cs = rd16(h + 22);
    x->rel_off = rd16(h + 24);
    x->high = !x->min_alloc && !x->max_alloc;
    if (x->nrel && x->rel_off + (unsigned long)x->nrel * 4 > file_size)
        return -1;
    return 0;
}

void dos_exe_memory(const struct dos_exe *x, unsigned long file_size, unsigned long *need, unsigned long *want)
{
    if (!x->exe) {
        *need = 0x10 + (file_size + 15) / 16;   /* (the PSP and the file; DOS gives a .COM the largest block) */
        *want = 0xFFFF;
        return;
    }
    *need = 0x10 + x->image_paras + x->min_alloc;
    *want = x->high ? 0xFFFF : 0x10 + x->image_paras + x->max_alloc;
    if (*want > 0xFFFF)
        *want = 0xFFFF;
    if (*want < *need)
        *want = *need;
}

unsigned dos_exe_image_seg(const struct dos_exe *x, unsigned psp, unsigned long paras)
{
    if (x->exe && x->high && paras >= 0x10 + x->image_paras)
        return (unsigned)(psp + paras - x->image_paras);
    return psp + 0x10;
}

unsigned dos_exe_relocate(unsigned char *mem, unsigned long mem_len, const unsigned char *rel, unsigned n,
                          unsigned factor)
{
    unsigned i, done = 0;

    for (i = 0; i < n; i++) {
        unsigned long at = (unsigned long)rd16(rel + 4 * i + 2) * 16 + rd16(rel + 4 * i);
        unsigned w;

        if (at + 2 > mem_len)
            continue;
        w = (rd16(mem + at) + factor) & 0xFFFF;
        mem[at] = (unsigned char)w;
        mem[at + 1] = (unsigned char)(w >> 8);
        done++;
    }
    return done;
}

void dos_exe_regs(const struct dos_exe *x, unsigned psp, unsigned long paras, int fcb1_ok, int fcb2_ok,
                  struct dos_regs0 *r)
{
    unsigned seg = dos_exe_image_seg(x, psp, paras);

    r->ax = (fcb1_ok ? 0 : 0xFF) | (fcb2_ok ? 0 : 0xFF00);
    r->ds = r->es = psp;
    if (!x->exe) {
        r->cs = r->ss = psp;
        r->ip = 0x100;
        r->sp = paras >= 0x1000 ? 0xFFFE : (unsigned)(paras * 16 - 2);
        r->push0 = 1;
        return;
    }
    r->cs = (seg + x->cs) & 0xFFFF;
    r->ip = x->ip;
    r->ss = (seg + x->ss) & 0xFFFF;
    r->sp = x->sp;
    r->push0 = 0;
}
