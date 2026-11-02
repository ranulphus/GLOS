/* NESTRM N (docs/upper-memory.md U3): a DPMI client that starts itself N levels deep, each a child client in its
 * parent's context, for the DPMI host's real-mode stacks. GLOS's log says which level had a slot of its page
 * (GLOS-DPMI start ... rm=page) and which gave its own memory (rm=own). Each level calls DOS through the host's
 * stack before and after its child, and prints, deepest first:
 *     HX-NESTRM depth=N version=V
 * DJGPP. */
#include <dpmi.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 3, v;
    char arg[12];
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x30;                              /* DOS's version, on the host's real-mode stack */
    __dpmi_int(0x21, &r);
    v = r.h.al;
    if (n > 1) {
        sprintf(arg, "%d", n - 1);
        if (spawnl(P_WAIT, argv[0], argv[0], arg, NULL) != 0) {
            printf("HX-NESTRM depth=%d spawn failed\n", n);
            return 1;
        }
    }
    memset(&r, 0, sizeof r);
    r.h.ah = 0x30;
    __dpmi_int(0x21, &r);
    printf("HX-NESTRM depth=%d version=%d\n", n, r.h.al == v ? v : -1);
    return 0;
}
