/* PROFCHK (GLOS M4e): what a program's profile (GLOS.CFG's [program
 * PROFCHK.EXE], supervisor.md §11.2) gives it. Reports on COM1:
 *     HX-PROF var=<GLOSPROF, or -> path=<PATH's first 8 bytes> largest=<KB> got=<KB>
 * largest is DPMI 0500h's largest block; got is what 0501h hands over in
 * 256 KB blocks before it fails (stopping at 256 MB). DJGPP. */
#include <dpmi.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        int n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 100000) ;
        outportb(0x3F8, *s);
    }
}

int main(void)
{
    __dpmi_free_mem_info fi;
    __dpmi_meminfo m;
    unsigned long got = 0;
    const char *v = getenv("GLOSPROF"), *p = getenv("PATH");
    char line[160];

    __dpmi_get_free_memory_information(&fi);
    while (got < 256ul * 1024) {
        m.size = 256 * 1024;
        if (__dpmi_allocate_memory(&m) != 0)
            break;
        got += 256;
    }
    snprintf(line, sizeof line, "HX-PROF var=%s path=%.8s largest=%lu got=%lu\r\n", v ? v : "-", p ? p : "-",
             fi.largest_available_free_block_in_bytes / 1024, got);
    ser(line);
    return 0;
}
