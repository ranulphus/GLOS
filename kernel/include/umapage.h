/* Which upper-memory page GLOS's stub may take (kernel/mm/umapage.c; docs/upper-memory.md, Which page): pure,
 * no kernel calls, so tests/host/umapage_test.c builds it as it is. */
#ifndef K_UMAPAGE_H
#define K_UMAPAGE_H
#include "types.h"

#define UP_LO       0xC0000u            /* where option ROMs are looked for */
#define UP_FIRST    0xC8000u            /* the first page a setting may name */
#define UP_AUTO_END 0xE0000u            /* auto stops here: E0000h-EFFFFh only by name */
#define UP_END      0xF0000u

enum { UP_OK, UP_RANGE, UP_ROM, UP_BAR, UP_DATA };

struct upage_env {
    void (*read)(u32 phys, u8 *out, u32 n);     /* bytes as they read now (the kernel: through the identity map) */
    const u32 *claims;                  /* addresses below 1 MB that a PCI BAR or expansion-ROM BAR starts at */
    u32 nclaims;
};

int upage_check(const struct upage_env *e, u32 phys);  /* UP_OK, or why the page at phys can't be had */
u32 upage_auto(const struct upage_env *e);              /* the page `auto` takes, or 0 */
u32 upage_next(const struct upage_env *e, u32 from, u32 end);  /* the first that qualifies in [from, end), or 0 */
const char *upage_why(int r);                           /* "rom", "bar", "data", "range" */

#endif
