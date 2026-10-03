/* kmain: the M1 kernel. Takes over the CPU tables and memory, runs the
 * kernel clock for about a second against PIT channel 2, reports, restores
 * the PIC and RTC as GLOS.EXE found them and returns to it (supervisor.md §2,
 * milestones-m0-m4.md M1). */
#include "glos/bootinfo.h"
#include "arch.h"
#include "gdb.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "nic.h"
#include "pci.h"
#include "random.h"
#include "sched.h"
#include "sshd.h"
#include "timer.h"
#include "vm.h"

#define WINDOWS 18                              /* 18 x 54.925 ms = 988.6 ms */

void cpu_set_df_cr3(u32 cr3);

/* /SELFTEST: a bulk thread that never blocks and an urgent one that sleeps
   10 ticks at a time, beside the VM. The report at leave shows the budget
   and the sleeper's lateness (tests/loopa/jobs.py sched). */
static volatile u32 burn_count, sleeper_wakes, sleeper_late_max;

static void burn(void *arg)
{
    (void)arg;
    for (;;)
        burn_count++;
}

static void sleeper(void *arg)
{
    u32 want = timer_ticks(), late;
    (void)arg;
    for (;;) {
        want += 10;
        thread_sleep_until(want);
        late = timer_ticks() - want;
        if (late > sleeper_late_max)
            sleeper_late_max = late;
        sleeper_wakes++;
    }
}

void selftest_report(void)
{
    kprintf("GLOS-SCHEDTEST wakes=%u late_max=%u burn=%u\n", sleeper_wakes, sleeper_late_max, burn_count);
}

void crypto_selftest(void);

static void kat_thread(void *arg)
{
    (void)arg;
    crypto_selftest();
}

static void selftest_start(void)
{
    thread_create("kat", PRIO_BULK, kat_thread, NULL);
    thread_create("burn", PRIO_BULK, burn, NULL);
    thread_create("sleeper", PRIO_URGENT, sleeper, NULL);
}

/* The kernel's own copy: GLOS.EXE frees its data once its resident stub is
   all that is left of it (supervisor.md §2.2). */
static struct bootinfo kbi;

void kmain(struct bootinfo *loader_bi)
{
    struct bootinfo *bi = &kbi;
    u32 t0, t1, expect, mhz = 0;
    u64 c0 = 0, c1 = 0;
    void *probe;
    int i, tsc;

    serial_init(COM1);
    if (loader_bi->magic != BOOTINFO_MAGIC || loader_bi->version != BOOTINFO_VERSION
        || loader_bi->size != sizeof kbi)
        leave_to_loader(3, loader_bi->pd_phys);
    kbi = *loader_bi;
    tsc = (bi->cpuid_edx & 0x10) != 0;
    kprintf("GLOS-RING0 step=entry mode=%s kernel=%p ranges=%u flags=%x\n",
            bi->mode == BI_MODE_XMS ? "xms" : "raw", bi->kernel_phys, bi->n_ranges, bi->flags);
    cpu_init(bi->cs_base, bi->ds_base, bi->ret_off);
    cpu_features();
    mm_init(bi);
    cpu_set_df_cr3(mm_cr3());
    probe = kmalloc(1000);
    kprintf("GLOS-RING0 step=mm free_kb=%u cr4=%s bits=%x heap=%p\n", pmm_free_frames() * 4,
            cpu_has_cr4 ? "yes" : "absent", cpu_cr4_bits, (u32)probe);
    kfree(probe);

    if (bi->flags & BI_F_GDB) {
        gdb_init(COM2);
        kprintf("GLOS-GDB waiting on COM2\n");
        gdb_breakpoint();
    }

    if (bi->flags & BI_F_VM) {                  /* DOS carries on in V86 mode, as a thread */
        sched_init();
        random_init(bi);
        vm_start(bi);
        pci_scan();
        if (ne2k_probe() == 0) {
            sshd_start(bi);                     /* before the net thread, which listens for it */
            net_start(bi->flags & BI_F_SELFTEST);
        }
        if (bi->flags & BI_F_SELFTEST)
            selftest_start();
        sched_idle();
    }

    /* The clock: all IRQs masked but the cascade and the RTC. */
    pic_init(IRQ_BASE_MASTER, IRQ_BASE_SLAVE, 0xFEFB);
    timer_start();
    sti();
    pit2_wait();                                /* settle */
    t0 = timer_ticks();
    if (tsc) c0 = rdtsc();
    for (i = 0; i < WINDOWS; i++)
        pit2_wait();
    t1 = timer_ticks();
    if (tsc) c1 = rdtsc();
    cli();
    timer_stop();
    pic_init(0x08, 0x70, (u16)bi->pic_mask);
    expect = (u32)(1024ull * WINDOWS * 54925 / 1000000);
    if (tsc)
        mhz = (u32)(c1 - c0) / (WINDOWS * 54925u);   /* fits in 32 bits below 4 GHz */
    kprintf("GLOS-RING0 ticks=%u window_ms=%u expect=%u spurious=%u tsc_mhz=%u\n",
            t1 - t0, WINDOWS * 54925 / 1000, expect, timer_spurious(), mhz);
    loader_bi->result = 0;
    leave_to_loader(0, bi->pd_phys);
}
