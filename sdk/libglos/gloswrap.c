/* libglos: one lock round DJGPP's C library, for programs with threads
 * (include/glos/glos.h; docs/supervisor.md 12.7).
 *
 * The program is linked with --wrap for each function in wrap.list, so that
 * every call of one, the library's own included, comes here first: the lock
 * is taken, the library's function called, the lock given. The lock is the
 * caller's as often as it takes it (the library calls itself: fopen calls
 * malloc). Until the program makes a thread (glos_thread) nothing is locked.
 *
 * The lock: a word swapped for 1 by whoever takes it (one instruction: a
 * thread can lose the processor anywhere else). A thread that finds it
 * taken counts itself among the waiters and waits for a signal of GLOS's
 * that lets one waiter go; the holder sets the signal when it gives the
 * lock and anyone waits. A set that comes before the wait stays for it, and
 * the wait is never longer than a tenth of a second, so no order of the two
 * can leave a waiter asleep with the lock free. The holder that woke a
 * waiter gives up the rest of its turn, so that the waiter gets the lock
 * before the holder is back for it. Without contention no call is made to
 * GLOS at all. */
#include <dir.h>
#include <dirent.h>
#include <dpmi.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/exceptn.h>
#include <sys/nearptr.h>
#include <sys/segments.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glos/glos.h>

extern int __glos_threaded;
unsigned __glos_self(void);

static volatile unsigned held, waiters;
static unsigned owner, depth;           /* owner: the holder (__glos_self() plus 1), written by the holder alone */
static glos_handle sig;

static inline unsigned swap(volatile unsigned *p, unsigned v)
{
    __asm__ __volatile__("xchgl %0, %1" : "+r"(v), "+m"(*p));
    return v;
}

/* The program is about to have threads (glos_thread, the first time; the first thread). Two things of DJGPP's
   are the first thread's alone, and are seen to here:
   - DJGPP turns an interrupt into a signal by shrinking the limit of the program's data segment in its interrupt
     handler; the next touch of memory faults, and its exception handler puts the limit back and raises the
     signal. A thread using that selector would take the fault instead. So the others run on DJGPP's second data
     selector (__djgpp_ds_alias, which its interrupt handlers use, and which is never shrunk).
   - sbrk() raises the limits of the selectors of the thread it runs in, and of no other. So the limits of the
     code segment and both data selectors are opened once, here, as far as they go, and sbrk() told so
     (__djgpp_selector_limit): it never has to raise one again. A stray pointer is still a fault: no page is
     there. */
void __glos_lock_start(void)
{
    unsigned long limit = 0xFFFFFFFFul - (unsigned long)__djgpp_base_address, data = limit;
    if (__dpmi_get_segment_limit(_my_ds()) > data)
        data = 0xFFFFFFFFul;            /* (near pointers are on, glos_ptr's: the data selectors reach everywhere) */
    if (__dpmi_set_segment_limit(_my_cs(), limit) == 0 && __dpmi_set_segment_limit(__djgpp_app_DS, data) == 0
        && __dpmi_set_segment_limit(__djgpp_ds_alias, data) == 0)
        __djgpp_selector_limit = (int)limit;
    sig = glos_signal(GLOS_SIGNAL_ONE);
    __glos_threaded = 1;
}

void glos_libc_lock(void)
{
    unsigned me;
    if (!__glos_threaded)
        return;
    me = __glos_self() + 1;
    if (owner == me) {                  /* (only its holder can read its own number there) */
        depth++;
        return;
    }
    while (swap(&held, 1)) {
        __asm__ __volatile__("incl %0" : "+m"(waiters));
        if (!swap(&held, 1)) {
            __asm__ __volatile__("decl %0" : "+m"(waiters));
            break;
        }
        if (sig)
            glos_wait(&sig, 1, 100, 0);
        else
            glos_sleep(1);
        __asm__ __volatile__("decl %0" : "+m"(waiters));
    }
    owner = me;
    depth = 1;
}

void glos_libc_unlock(void)
{
    if (!__glos_threaded || owner != __glos_self() + 1)
        return;                         /* (taken before the program had threads: nothing was) */
    if (--depth)
        return;
    owner = 0;
    held = 0;
    if (waiters && sig) {
        glos_signal_set(sig, 1);
        glos_yield();                   /* (the waiter's turn: or this thread takes the lock again before the waiter
                                           has run, time after time) */
    }
}

/* ---- the wrapped functions */

#define W(ret, name, params, args) \
    ret __real_##name params; \
    ret __wrap_##name params; \
    ret __wrap_##name params \
    { \
        ret r; \
        glos_libc_lock(); \
        r = __real_##name args; \
        glos_libc_unlock(); \
        return r; \
    }
#define WV(name, params, args) \
    void __real_##name params; \
    void __wrap_##name params; \
    void __wrap_##name params \
    { \
        glos_libc_lock(); \
        __real_##name args; \
        glos_libc_unlock(); \
    }

W(void *, malloc, (size_t n), (n))
WV(free, (void *p), (p))
W(void *, calloc, (size_t n, size_t m), (n, m))
W(void *, realloc, (void *p, size_t n), (p, n))

W(FILE *, fopen, (const char *path, const char *mode), (path, mode))
W(FILE *, freopen, (const char *path, const char *mode, FILE *f), (path, mode, f))
W(FILE *, fdopen, (int fd, const char *mode), (fd, mode))
W(int, fclose, (FILE *f), (f))
W(int, fflush, (FILE *f), (f))
W(size_t, fread, (void *p, size_t size, size_t n, FILE *f), (p, size, n, f))
W(size_t, fwrite, (const void *p, size_t size, size_t n, FILE *f), (p, size, n, f))
W(char *, fgets, (char *s, int n, FILE *f), (s, n, f))
W(int, fputs, (const char *s, FILE *f), (s, f))
W(int, fgetc, (FILE *f), (f))
W(int, fputc, (int c, FILE *f), (c, f))
W(int, ungetc, (int c, FILE *f), (c, f))
W(int, puts, (const char *s), (s))
W(int, fseek, (FILE *f, long off, int whence), (f, off, whence))
W(long, ftell, (FILE *f), (f))
WV(rewind, (FILE *f), (f))
W(int, fgetpos, (FILE *f, fpos_t *pos), (f, pos))
W(int, fsetpos, (FILE *f, const fpos_t *pos), (f, pos))
W(int, setvbuf, (FILE *f, char *buf, int mode, size_t size), (f, buf, mode, size))
WV(setbuf, (FILE *f, char *buf), (f, buf))
WV(perror, (const char *s), (s))
W(int, remove, (const char *path), (path))
W(int, rename, (const char *from, const char *to), (from, to))
W(FILE *, tmpfile, (void), ())

W(int, vprintf, (const char *fmt, va_list ap), (fmt, ap))
W(int, vfprintf, (FILE *f, const char *fmt, va_list ap), (f, fmt, ap))
W(int, vsprintf, (char *s, const char *fmt, va_list ap), (s, fmt, ap))
W(int, vsnprintf, (char *s, size_t n, const char *fmt, va_list ap), (s, n, fmt, ap))
W(int, vfscanf, (FILE *f, const char *fmt, va_list ap), (f, fmt, ap))
W(int, vsscanf, (const char *s, const char *fmt, va_list ap), (s, fmt, ap))

/* Those with a variable number of arguments: through the library's own va_list forms, wrapped above. */
#define WF(name, params, last, call) \
    int __wrap_##name params; \
    int __wrap_##name params \
    { \
        va_list ap; \
        int r; \
        va_start(ap, last); \
        r = call; \
        va_end(ap); \
        return r; \
    }
WF(printf, (const char *fmt, ...), fmt, __wrap_vfprintf(stdout, fmt, ap))
WF(fprintf, (FILE *f, const char *fmt, ...), fmt, __wrap_vfprintf(f, fmt, ap))
WF(sprintf, (char *s, const char *fmt, ...), fmt, __wrap_vsprintf(s, fmt, ap))
WF(snprintf, (char *s, size_t n, const char *fmt, ...), fmt, __wrap_vsnprintf(s, n, fmt, ap))
WF(scanf, (const char *fmt, ...), fmt, __wrap_vfscanf(stdin, fmt, ap))
WF(fscanf, (FILE *f, const char *fmt, ...), fmt, __wrap_vfscanf(f, fmt, ap))
WF(sscanf, (const char *s, const char *fmt, ...), fmt, __wrap_vsscanf(s, fmt, ap))

int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...)
{
    va_list ap;
    int mode, r;
    va_start(ap, flags);
    mode = va_arg(ap, int);             /* (read whether it was given or not, as the library's own does) */
    va_end(ap);
    glos_libc_lock();
    r = __real_open(path, flags, mode);
    glos_libc_unlock();
    return r;
}
W(int, creat, (const char *path, mode_t mode), (path, mode))
W(int, close, (int fd), (fd))
W(ssize_t, read, (int fd, void *buf, size_t n), (fd, buf, n))
W(ssize_t, write, (int fd, const void *buf, size_t n), (fd, buf, n))
W(off_t, lseek, (int fd, off_t off, int whence), (fd, off, whence))
W(int, dup, (int fd), (fd))
W(int, dup2, (int fd, int to), (fd, to))
W(int, unlink, (const char *path), (path))
W(int, stat, (const char *path, struct stat *st), (path, st))
W(int, fstat, (int fd, struct stat *st), (fd, st))
W(int, access, (const char *path, int mode), (path, mode))
W(int, mkdir, (const char *path, mode_t mode), (path, mode))
W(int, rmdir, (const char *path), (path))
W(int, chdir, (const char *path), (path))
W(char *, getcwd, (char *buf, size_t n), (buf, n))
W(int, chmod, (const char *path, mode_t mode), (path, mode))
W(int, ftruncate, (int fd, off_t n), (fd, n))
W(int, fsync, (int fd), (fd))
W(DIR *, opendir, (const char *path), (path))
W(struct dirent *, readdir, (DIR *d), (d))
W(int, closedir, (DIR *d), (d))
WV(rewinddir, (DIR *d), (d))
W(int, findfirst, (const char *path, struct ffblk *ff, int attr), (path, ff, attr))
W(int, findnext, (struct ffblk *ff), (ff))
W(int, rand, (void), ())
WV(srand, (unsigned seed), (seed))

/* DJGPP's last step of exit() and of every other end of the program (after atexit's functions, the files' close
   and its handlers' removal): crt0's __exit, which frees the program's selectors and memory and then ends it.
   The threads the program made run on one of those selectors, so in a program with threads that step is left
   out: the program ends by DOS's function at once, from whichever thread got here, and GLOS stops the other
   threads and drops the selectors and the memory with the program's whole context (as it does for a kill). */
void __real___exit(int status) __attribute__((noreturn));
void __wrap___exit(int status) __attribute__((noreturn));
void __wrap___exit(int status)
{
    if (__glos_threaded)
        for (;;)
            __asm__ __volatile__("int $0x21" : : "a"(0x4C00u | ((unsigned)status & 0xFFu)));
    __real___exit(status);
}
