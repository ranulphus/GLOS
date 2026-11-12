/* libglos's threads (include/glos/glos.h; API 2.0's THREAD).
 *
 * A thread's stack is the C library's memory, kept in a table here with its
 * handle: freed when the thread is joined, or, detached, once it is found
 * ended (at the next glos_thread() or glos_join()). The table also says
 * which thread is running, by where its stack pointer is: the C library's
 * lock (gloswrap.c) is taken again by its holder, and has no other way to
 * know who asks. Using this file brings that lock into the program, and
 * with it the need for the linker's options (gloswrap.opt). */
#include <stdlib.h>
#include <string.h>
#include <sys/exceptn.h>

#include <glos/glos.h>

#define STACK_DEFAULT 65536u
#define STACK_MIN     16384u

struct thr {
    volatile int state;                 /* 0 free, 1 running or ended (to be joined), 2 detached */
    glos_handle h;
    char *stack;
    unsigned size;
    void (*fn)(void *);
    void *arg;
};

static struct thr thr[GLOS_THREADS];
int __glos_threaded;                    /* the program has made a thread: the C library's lock is used from now */

void __glos_error_set(int e);
unsigned __glos_call_eax(unsigned fn);  /* glos.c: a call with no structure, whose answer is EAX (0 if refused) */
void __glos_lock_start(void);           /* gloswrap.c */

/* The running thread: 0 the first, or the table's entry plus 1. */
unsigned __glos_self(void)
{
    unsigned sp = (unsigned)__builtin_frame_address(0), i;
    for (i = 0; i < GLOS_THREADS; i++)
        if (thr[i].state && sp - (unsigned)thr[i].stack < thr[i].size)
            return i + 1;
    return 0;
}

static void start(void *p)
{
    struct thr *t = p;
    t->fn(t->arg);
    glos_thread_exit(0);
}

static int ended(glos_handle h, unsigned ms, unsigned *code)
{
    struct glos_thread_join q;
    int r;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.handle = h;
    q.ms = ms;
    if ((r = glos_call(GLOS_THREAD_JOIN, &q)) != 0)
        return -r;
    if (q.ended && code)
        *code = q.code;
    return (int)q.ended;
}

static void drop(struct thr *t)
{
    char *stack = t->stack;
    glos_close(t->h);
    glos_libc_lock();
    t->state = 0;
    t->stack = 0;
    t->size = 0;
    glos_libc_unlock();
    free(stack);
}

static void reap(void)                  /* detached threads that have ended */
{
    unsigned i;
    for (i = 0; i < GLOS_THREADS; i++)
        if (thr[i].state == 2 && ended(thr[i].h, 0, 0) != 0)
            drop(&thr[i]);
}

glos_handle glos_thread(void (*fn)(void *arg), void *arg, unsigned stack, unsigned flags)
{
    struct glos_thread q;
    struct thr *t = 0;
    char *mem;
    unsigned i;
    int r;
    if (!stack)
        stack = STACK_DEFAULT;
    if (stack < STACK_MIN)
        stack = STACK_MIN;
    stack = (stack + 15) & ~15u;
    reap();
    if (!__glos_threaded)
        __glos_lock_start();            /* (before the first thread is there: nobody else can be in the library) */
    if (!(mem = malloc(stack))) {
        __glos_error_set(GLOS_E_MEMORY);
        return 0;
    }
    glos_libc_lock();                   /* (the table: threads may make threads) */
    for (i = 0; i < GLOS_THREADS && !t; i++)
        if (!thr[i].state)
            t = &thr[i];
    if (t) {
        t->fn = fn;
        t->arg = arg;
        t->stack = mem;
        t->size = stack;
        t->h = 0;
        t->state = 1;
    }
    glos_libc_unlock();
    if (!t) {
        free(mem);
        __glos_error_set(GLOS_E_MEMORY);
        return 0;
    }
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.entry = (unsigned)start;
    q.arg = (unsigned)t;
    q.stack = (((unsigned)mem + stack) & ~15u) - 12;   /* (as a call leaves it: the argument on a 16-byte boundary) */
    q.flags = flags;
    q.data = __djgpp_ds_alias;          /* not the program's DS, which each of DJGPP's signals shrinks: gloswrap.c */
    if ((r = glos_call(GLOS_THREAD_CREATE, &q)) != 0) {
        glos_libc_lock();
        t->state = 0;
        t->stack = 0;
        t->size = 0;
        glos_libc_unlock();
        free(mem);
        __glos_error_set(r);
        return 0;
    }
    t->h = q.handle;
    return q.handle;
}

void glos_thread_exit(unsigned code)
{
    struct glos_thread_exit q = { sizeof q, code };
    glos_call(GLOS_THREAD_EXIT, &q);
    for (;;)                            /* (the first thread, refused: its end is the program's) */
        exit((int)code);
}

static struct thr *find(glos_handle h)
{
    unsigned i;
    for (i = 0; i < GLOS_THREADS; i++)
        if (thr[i].state && thr[i].h == h)
            return &thr[i];
    return 0;
}

int glos_join(glos_handle h, unsigned ms, unsigned *code)
{
    struct thr *t = find(h);
    int r = ended(h, ms, code);
    if (r == 1 && t)
        drop(t);
    reap();
    return r;
}

int glos_detach(glos_handle h)
{
    struct thr *t = find(h);
    if (!t)
        return GLOS_E_HANDLE;
    t->state = 2;
    reap();
    return 0;
}

unsigned glos_self(void) { return __glos_call_eax(GLOS_THREAD_SELF); }

glos_handle glos_lock(void)
{
    struct glos_lock q = { sizeof q, 0 };
    int r = glos_call(GLOS_THREAD_LOCK, &q);
    if (r)
        __glos_error_set(r);
    return r ? 0 : q.handle;
}

int glos_take(glos_handle lock, unsigned ms)
{
    struct glos_take q = { sizeof q, lock, ms, 0 };
    int r = glos_call(GLOS_THREAD_TAKE, &q);
    return r ? -r : (int)q.taken;
}

int glos_give(glos_handle lock)
{
    struct glos_give q = { sizeof q, lock };
    return glos_call(GLOS_THREAD_GIVE, &q);
}

glos_handle glos_signal(unsigned flags)
{
    struct glos_signal q = { sizeof q, flags, 0 };
    int r = glos_call(GLOS_THREAD_SIGNAL, &q);
    if (r)
        __glos_error_set(r);
    return r ? 0 : q.handle;
}

int glos_signal_set(glos_handle signal, int set)
{
    struct glos_signal_set q = { sizeof q, signal, set != 0 };
    return glos_call(GLOS_THREAD_SET, &q);
}

int glos_wait(const glos_handle *handles, unsigned n, unsigned ms, int *which)
{
    struct glos_wait_any q;
    int r;
    if (which)
        *which = -1;
    if (!n || n > GLOS_WAIT_MAX)
        return GLOS_E_ARGUMENT;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.count = n;
    memcpy(q.handles, handles, n * sizeof *handles);
    q.ms = ms;
    q.which = -1;
    r = glos_call(GLOS_THREAD_WAIT, &q);
    if (!r && which)
        *which = q.which;
    return r;
}
