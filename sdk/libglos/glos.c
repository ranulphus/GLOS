/* libglos: the GLOS API for DJGPP programs (include/glos/api.h; PRD §10).
 * The entry is found once, with DPMI 0A00h "GLOS"; without GLOS (plain DOS,
 * another DPMI host) every function fails or does nothing, so a program can
 * call glos_init() and carry on as a plain DOS program when it returns -1. */
#include <dpmi.h>

#include <glos/api.h>

static struct {
    unsigned long off;
    unsigned short sel;
} entry;
static int state;                       /* 1: GLOS is there; -1: it isn't */
static int error, reason;
static unsigned id;

static int find(void)
{
    __dpmi_paddr p;
    if (!state) {
        if (__dpmi_get_vendor_specific_api_entry_point("GLOS", &p) == 0) {
            entry.off = p.offset32;
            entry.sel = p.selector;
            state = 1;
        } else {
            state = -1;
        }
    }
    return state;
}

/* Function fn with b in EBX; EAX's result in *a, EBX's in *b: 0, or -1 with
   error (and reason) set. */
static int call(unsigned fn, unsigned *a, unsigned *b)
{
    unsigned char failed;
    unsigned c = 0, d = 0;
    if (find() < 0) {
        error = 0;
        return -1;
    }
    *a = fn;
    __asm__ __volatile__("lcall *%[e]\n\tsetc %[f]"
                         : "+a"(*a), "+b"(*b), "+c"(c), "+d"(d), [f] "=m"(failed)
                         : [e] "m"(entry)
                         : "esi", "edi", "cc", "memory");
    if (failed) {
        error = (int)*a;
        reason = (int)*b;
        return -1;
    }
    return 0;
}

unsigned glos_version(void)
{
    unsigned a, b = 0;
    return call(GLOS_FN_VERSION, &a, &b) == 0 ? a : 0;
}

int glos_init(void)
{
    unsigned a, b = GLOS_API_VERSION;
    if (call(GLOS_FN_INIT, &a, &b) != 0)
        return -1;
    id = b;
    return 0;
}

int glos_error(void) { return error; }
int glos_reason(void) { return reason; }
unsigned glos_id(void) { return id; }

void glos_yield(void)
{
    unsigned a, b = 0;
    call(GLOS_FN_YIELD, &a, &b);
}

void glos_sleep(unsigned ms)
{
    unsigned a, b = ms;
    call(GLOS_FN_SLEEP, &a, &b);
}

unsigned glos_ticks(void)
{
    unsigned a, b = 0;
    return call(GLOS_FN_TICKS, &a, &b) == 0 ? a : 0;
}
