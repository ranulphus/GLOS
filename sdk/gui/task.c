/* Background work (toolkit 2, step C4; docs/v1/toolkit-2.md 3): a task is a function run on a thread of the
 * app's own (API 2.0's THREAD), so that the window's thread goes on taking events while a folder is read, a
 * file copied, a picture made small.
 *
 * Only the window's thread touches widgets. A task tells it things through here: its progress (the latest,
 * not every step), results as they come (gui_task_post: a function run on the window's thread, in the order
 * posted), and its end (the task's `done`). They cross in a queue under a lock; one event of the app's own
 * (glos_post) wakes the window's thread, which empties the queue.
 *
 * The workers are a pool: made when first needed, two by default and one where the CPU is a 486
 * (gui_task_threads says otherwise); a task waits for a free worker. A task with GUI_TASK_DIALOG that is still
 * running a second after it began has a dialog over its window: its title, a bar, its line of text, Cancel.
 * Cancelling is asked (gui_task_cancel, the dialog's button) and the work looks for it (gui_task_cancelled).
 *
 * A program that uses tasks is linked with @gloswrap.opt (libglos's lock round the C library). */
#include <stdio.h>

#include "guiint.h"

#define POSTS 128
#define WORKERS_MAX 4
#define DIALOG_AFTER 1000

struct gui_task {
    struct gui_task *qnext;             /* among those that wait for a worker (under the lock) */
    struct gui_task *lnext;             /* among all not yet ended (the window's thread's own list) */
    struct gui_window *owner;
    char title[64];
    gui_task_fn work;
    void *arg;
    void (*done)(struct gui_window *owner, void *arg, int cancelled);
    unsigned flags;
    volatile int cancel;
    volatile int state;                 /* 0 waits for a worker, 1 runs, 2 has returned */
    int p_done, p_total, p_new;         /* its progress, under the lock */
    char p_text[96];
    unsigned began;
    struct gui_window *dialog;
    struct gui_widget *bar, *line;
};

static struct {
    glos_handle lock, wake;
    glos_handle thr[WORKERS_MAX];
    int nthr, want;
    struct gui_task *head, *tail;       /* waiting for a worker */
    struct gui_task *live;              /* every task not yet ended, for the window's thread */
    struct {
        void (*fn)(void *);
        void *data;
        struct gui_task *task;          /* fn NULL: this task has returned */
    } post[POSTS];
    unsigned ph, pt;
    int woken;                          /* an event to the window's thread is on its way */
    int ticking;
} K;

#define TASK_CODE 0x54534B21u           /* the toolkit's own event code ("TSK!") */

static void wake_window(void)           /* (under the lock) */
{
    if (!K.woken) {
        K.woken = 1;
        glos_post(TASK_CODE, 0, 0, 0);
    }
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        struct gui_task *t;
        int which;
        glos_take(K.lock, 0xFFFFFFFFu);
        if ((t = K.head) != NULL) {
            K.head = t->qnext;
            if (!K.head)
                K.tail = NULL;
            t->state = 1;
        }
        glos_give(K.lock);
        if (!t) {
            glos_wait(&K.wake, 1, 0xFFFFFFFFu, &which);
            continue;
        }
        t->work(t, t->arg);
        glos_take(K.lock, 0xFFFFFFFFu);
        t->state = 2;
        while (K.pt - K.ph >= POSTS) {          /* (the queue full: the window's thread is behind) */
            glos_give(K.lock);
            glos_sleep(10);
            glos_take(K.lock, 0xFFFFFFFFu);
        }
        K.post[K.pt % POSTS].fn = NULL, K.post[K.pt % POSTS].data = NULL, K.post[K.pt % POSTS].task = t;
        K.pt++;
        wake_window();
        glos_give(K.lock);
    }
}

void gui_task_threads(int n) { K.want = n < 1 ? 1 : n > WORKERS_MAX ? WORKERS_MAX : n; }

static int start_pool(void)
{
    if (!K.lock && (!(K.lock = glos_lock()) || !(K.wake = glos_signal(GLOS_SIGNAL_ONE))))
        return -1;
    if (!K.want) {
        struct glos_sys_system sys;
        unsigned len = 0, count = 0;
        memset(&sys, 0, sizeof sys);
        sys.size = sizeof sys;
        K.want = glos_sys_stat(GLOS_STAT_SYSTEM, &sys, sizeof sys, &len, &count) >= 0 && sys.cpu_family
                 && sys.cpu_family <= 4 ? 1 : 2;       /* (one on a 486: its one slow CPU is the window's too) */
    }
    while (K.nthr < K.want) {
        glos_handle h = glos_thread(worker, NULL, 0, 0);
        if (!h)
            break;
        K.thr[K.nthr++] = h;
    }
    return K.nthr ? 0 : -1;
}

/* ---- from the work (a worker's thread) */

void gui_task_progress(struct gui_task *t, int done, int total, const char *text)
{
    glos_take(K.lock, 0xFFFFFFFFu);
    t->p_done = done, t->p_total = total, t->p_new = 1;
    if (text)
        snprintf(t->p_text, sizeof t->p_text, "%s", text);
    wake_window();
    glos_give(K.lock);
}

int gui_task_cancelled(struct gui_task *t) { return t->cancel; }

void gui_task_post(struct gui_task *t, void (*fn)(void *data), void *data)
{
    glos_take(K.lock, 0xFFFFFFFFu);
    while (K.pt - K.ph >= POSTS) {
        glos_give(K.lock);
        glos_sleep(10);
        glos_take(K.lock, 0xFFFFFFFFu);
    }
    K.post[K.pt % POSTS].fn = fn, K.post[K.pt % POSTS].data = data, K.post[K.pt % POSTS].task = t;
    K.pt++;
    wake_window();
    glos_give(K.lock);
}

/* ---- on the window's thread */

static void cancel_click(struct gui_widget *w, int what)
{
    (void)what;
    gui_task_cancel(w->user);
}

static void dialog_close(struct gui_window *w)  /* (its close box: Cancel) */
{
    gui_task_cancel(w->user);
}

static void dialog_up(struct gui_task *t)
{
    struct gui_window *d = gui_window(t->title, 300, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(d->root, -1, -1), *row, *b;
    d->owner = t->owner;
    d->user = t;
    d->on_close = dialog_close;
    t->line = gui_label(box, t->p_text[0] ? t->p_text : " ");
    gui_size(t->line, 280, 0);
    t->bar = gui_progress(box, 100);
    gui_size(t->bar, 280, 0);
    row = gui_hbox(box, -1, 0);
    gui_space(row, 0, 0);
    b = gui_button(row, GUI_T("Cancel"), cancel_click, 0);
    b->user = t;
    b->flags |= GUI_CANCEL;
    t->dialog = d;
    gui_progress_set(t->bar, t->p_done, t->p_total > 0 ? t->p_total : 100);
    gui_modal_on(d);
}

static void dialog_down(struct gui_task *t)
{
    if (t->dialog) {
        struct gui_window *d = t->dialog;
        t->dialog = NULL;
        gui_modal_off(d);
        gui_close(d);
    }
}

/* Twice a second while tasks live: the dialog of one that has run a second. */
static void tick(void *arg)
{
    struct gui_task *t;
    (void)arg;
    for (t = K.live; t; t = t->lnext)
        if ((t->flags & GUI_TASK_DIALOG) && !t->dialog && t->state != 2 && gui_now() - t->began >= DIALOG_AFTER)
            dialog_up(t);
    if (!K.live) {
        gui_idle(tick, NULL, 0);
        K.ticking = 0;
    }
}

/* The app's own event: what the tasks have for the window's thread, in order. 1 if it was the toolkit's. */
static int task_event(unsigned code)
{
    struct gui_task *t, **pp;
    if (code != TASK_CODE || !K.lock)
        return 0;
    for (;;) {
        void (*fn)(void *) = NULL;
        void *data = NULL;
        struct gui_task *task = NULL;
        int got = 0;
        glos_take(K.lock, 0xFFFFFFFFu);
        if (K.ph != K.pt) {
            fn = K.post[K.ph % POSTS].fn, data = K.post[K.ph % POSTS].data, task = K.post[K.ph % POSTS].task;
            K.ph++;
            got = 1;
        } else {
            K.woken = 0;
        }
        glos_give(K.lock);
        if (!got)
            break;
        if (fn) {
            fn(data);
            continue;
        }
        dialog_down(task);                      /* it has returned: its end */
        for (pp = &K.live; *pp && *pp != task; pp = &(*pp)->lnext)
            ;
        if (*pp)
            *pp = task->lnext;
        if (task->done)
            task->done(task->owner, task->arg, task->cancel);
        free(task);
    }
    for (t = K.live; t; t = t->lnext) {         /* progress: the latest */
        int done = 0, total = 0, fresh;
        char text[96];
        glos_take(K.lock, 0xFFFFFFFFu);
        fresh = t->p_new;
        t->p_new = 0;
        done = t->p_done, total = t->p_total;
        memcpy(text, t->p_text, sizeof text);
        glos_give(K.lock);
        if (fresh && t->dialog) {
            gui_progress_set(t->bar, done, total > 0 ? total : 100);
            gui_set_text(t->line, text[0] ? text : " ");
        }
    }
    return 1;
}

static void libc_lock(int on)
{
    if (on)
        glos_libc_lock();
    else
        glos_libc_unlock();
}

struct gui_task *gui_task_start(struct gui_window *owner, const char *title, gui_task_fn work, void *arg,
                                void (*done)(struct gui_window *owner, void *arg, int cancelled), unsigned flags)
{
    struct gui_task *t;
    if (start_pool() != 0 || !(t = calloc(1, sizeof *t)))
        return NULL;
    gui_post_hook = task_event;
    gui_libc_hook = libc_lock;
    t->owner = owner, t->work = work, t->arg = arg, t->done = done, t->flags = flags;
    snprintf(t->title, sizeof t->title, "%s", title ? title : "");
    t->began = gui_now();
    t->lnext = K.live;
    K.live = t;
    glos_take(K.lock, 0xFFFFFFFFu);
    if (K.tail)
        K.tail->qnext = t;
    else
        K.head = t;
    K.tail = t;
    glos_give(K.lock);
    glos_signal_set(K.wake, 1);                 /* (a worker that waits goes on; one that works finds it after) */
    if (!K.ticking) {
        K.ticking = 1;
        gui_idle(tick, NULL, 500);
    }
    return t;
}

void gui_task_cancel(struct gui_task *t)
{
    t->cancel = 1;
    if (t->dialog && t->line)
        gui_set_text(t->line, GUI_T("Cancelling..."));
}
