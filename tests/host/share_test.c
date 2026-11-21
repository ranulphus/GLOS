/* share_test: kernel/ndos/share.c against RBIL's own text of table 01404 (DOS 7.x file sharing), held here as
 * RBIL release 61 has it: every one of the 400 cells, and modes outside the table refused.
 * "share_test: 0 failures" when all held. */
#include <stdio.h>
#include <string.h>

#include "../../kernel/ndos/share.h"

/* RBIL 61, INTERRUP.F, table 01404's rows (tabs as spaces), first open's sharing mode by access. */
static const char *const rbil[20] = {
    " Compat R |Y Y Y Y   N N N N   Y N N Y     N N N Y   Y N N Y",
    "    W |Y Y Y C   N N N N   N N N N     N N N Y   Y N N Y",
    "    RW|Y Y Y C   N N N N   N N N N     N N N Y   Y N N Y",
    "    NA|Y C C Y   N N N N   Y N N Y     N N N Y   Y N N Y",
    " Deny    R |C C C C   N N N N   N N N N     N N N N   N N N N",
    " All    W |C C C C   N N N N   N N N N     N N N N   N N N N",
    "    RW|C C C C   N N N N   N N N N     N N N N   N N N N",
    "    NA|C C C C   N N N N   N N N N     N N N N   N N N N",
    " Deny    R |Y C C Y   N N N N   Y N N Y     N N N Y   Y N N Y",
    " Write    W |C C C C   N N N N   N N N N     Y N N Y   Y N N Y",
    "    RW|C C C C   N N N N   N N N N     N N N Y   Y N N Y",
    "    NA|Y C C Y   N N N N   Y N N Y     N N N Y   Y N N Y",
    " Deny    R |C C C C   N N N N   N Y N N     N N N N   N Y N N",
    " Read    W |C C C C   N N N N   N N N N     N Y N N   N Y N N",
    "    RW|C C C C   N N N N   N N N N     N N N N   N Y N N",
    "    NA|Y Y Y Y   N N N N   Y Y Y Y     N N N Y   Y Y Y Y",
    " Deny    R |Y Y Y Y   N N N N   Y Y Y Y     N N N Y   Y Y Y Y",
    " None    W |C C C C   N N N N   N N N N     Y Y Y Y   Y Y Y Y",
    "    RW|C C C C   N N N N   N N N N     N N N Y   Y Y Y Y",
    "    NA|Y Y Y Y   N N N N   Y Y Y Y     N N N Y   Y Y Y Y",
};

int main(void)
{
    static const unsigned access[4] = { 0, 1, 2, 4 };
    int fails = 0, r, k, n;

    for (r = 0; r < 20; r++) {
        const char *p = strchr(rbil[r], '|');
        char cells[20];

        for (n = 0, p = p ? p + 1 : ""; *p && n < 20; p++)
            if (*p == 'Y' || *p == 'N' || *p == 'C')
                cells[n++] = *p;
        if (n != 20) {
            printf("FAIL row %d: %d cells\n", r, n);
            fails++;
            continue;
        }
        for (k = 0; k < 20; k++) {
            unsigned first = (unsigned)(r / 4) << 4 | access[r % 4], second = (unsigned)(k / 4) << 4 | access[k % 4];
            int want = cells[k] == 'Y' ? DOS_SHARE_YES : cells[k] == 'C' ? DOS_SHARE_INT24 : DOS_SHARE_NO;
            int got = dos_share(first, second);

            if (got != want) {
                printf("FAIL first %02x second %02x: %d, RBIL %c\n", first, second, got, cells[k]);
                fails++;
            }
        }
    }
    if (dos_share(0x03, 0x00) != DOS_SHARE_NO || dos_share(0x00, 0x50) != DOS_SHARE_NO ||
        dos_share(0x70, 0x00) != DOS_SHARE_NO) {
        printf("FAIL modes outside the table\n");
        fails++;
    }
    if (dos_share(0x42, 0x40) != DOS_SHARE_YES || dos_share(0x22, 0x41) != DOS_SHARE_NO ||
        dos_share(0x10, 0x00) != DOS_SHARE_INT24) {
        printf("FAIL spot checks\n");
        fails++;
    }
    printf("share_test: %d failures\n", fails);
    return fails != 0;
}
