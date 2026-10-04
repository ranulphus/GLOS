/* CRASHME [gp]: a DJGPP program that faults with no handler of its own, for
 * GLOS's crash report (M4b): it gives its exception handlers back to the
 * host (__djgpp_exception_toggle) and writes through a null pointer, into
 * the page DJGPP's crt0 uncommitted with 0507h, or with "gp" reads CR0 at
 * ring 3. The host must end it with a report whose EIP tools/symcrash.py
 * names as crash_here (or crash_gp), exit code 255, and the vectors as the
 * program found them. Built with DJGPP, with -g. */
#include <stdio.h>
#include <string.h>
#include <sys/exceptn.h>

volatile int *volatile null_ptr;

__attribute__((noinline)) void crash_here(int v)
{
    *null_ptr = v;
}

__attribute__((noinline)) void crash_gp(void)
{
    __asm__ volatile("movl %%cr0, %%eax" ::: "eax");
}

int main(int argc, char **argv)
{
    printf("CRASHME: faulting now\n");
    fflush(stdout);
    __djgpp_exception_toggle();
    if (argc > 1 && !strcmp(argv[1], "gp"))
        crash_gp();
    else
        crash_here(argc);
    printf("CRASHME: still here\n");
    return 0;
}
