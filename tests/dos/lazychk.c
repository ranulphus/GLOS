/* LAZYCHK (GLOS M4e): DPMI memory backed when first touched, as under
 * CWSDPMI (supervisor.md §12.3). DJGPP. On COM1:
 *     HX-LAZY largest=KB first=0|1 second=0|1 zero=0|1 kept=0|1 huge=0|1
 * largest is 0500h's largest block; first, whether 0501h hands over all of
 * it; second, whether 0501h then hands over 8 MB more (more than is free,
 * were the first block's pages taken at once); zero, whether pages read as
 * zero when first touched (one in 16 of the first block, all of the
 * second); kept, whether what was written there reads back; huge, whether
 * 0501h hands over four times the largest block besides (GLOS: no, it
 * caps what isn't backed yet at three times the memory there is). */
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdio.h>
#include <sys/farptr.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        int n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 100000) ;
        outportb(0x3F8, *s);
    }
}

static int sel_for(unsigned long lin, unsigned long size)
{
    int sel = __dpmi_allocate_ldt_descriptors(1);
    if (sel < 0)
        return -1;
    __dpmi_set_segment_base_address(sel, lin);
    __dpmi_set_segment_limit(sel, size - 1);
    return sel;
}

/* Every step-th page: zero when first read? then a pattern, read back. */
static void touch(unsigned long lin, unsigned long size, unsigned long step, int *zero, int *kept)
{
    int sel = sel_for(lin, size);
    unsigned long off;
    if (sel < 0) {
        *zero = *kept = 0;
        return;
    }
    for (off = 0; off < size; off += 4096 * step)
        if (_farpeekl(sel, off) != 0 || _farpeekl(sel, off + 4092) != 0)
            *zero = 0;
    for (off = 0; off < size; off += 4096 * step)
        _farpokel(sel, off + 8, off ^ 0x5A5A5A5Au);
    for (off = 0; off < size; off += 4096 * step)
        if (_farpeekl(sel, off + 8) != (off ^ 0x5A5A5A5Au))
            *kept = 0;
    __dpmi_free_ldt_descriptor(sel);
}

int main(void)
{
    __dpmi_free_mem_info fi;
    __dpmi_meminfo a, b, c;
    int first, second, huge, zero = 1, kept = 1;
    unsigned long largest;
    char line[160];

    __dpmi_get_free_memory_information(&fi);
    largest = fi.largest_available_free_block_in_bytes & ~0xFFFul;
    a.size = largest;
    first = __dpmi_allocate_memory(&a) == 0;
    b.size = 8ul << 20;
    second = __dpmi_allocate_memory(&b) == 0;
    c.size = largest * 4;
    huge = __dpmi_allocate_memory(&c) == 0;
    if (huge)
        __dpmi_free_memory(c.handle);
    if (first)
        touch(a.address, a.size, 16, &zero, &kept);
    if (second)
        touch(b.address, b.size, 1, &zero, &kept);
    snprintf(line, sizeof line, "HX-LAZY largest=%lu first=%d second=%d zero=%d kept=%d huge=%d\r\n", largest >> 10,
             first, second, zero, kept, huge);
    ser(line);
    if (second) __dpmi_free_memory(b.handle);
    if (first) __dpmi_free_memory(a.handle);
    return 0;
}
