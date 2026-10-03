/* Interrupt and exception entry, the #DF task, leaving to the loader, and
 * kernel helpers that may fault (supervisor.md §3, §6). */
        .code32
        .text

/* 256 stubs, 16 bytes apart: an error code where the CPU pushes none, then
   the vector. */
        .macro  stub n
        .align  16
        .if     (\n == 8) || ((\n >= 10) && (\n <= 14)) || (\n == 17)
        .else
        pushl   $0
        .endif
        pushl   $\n
        jmp     trap_common
        .endm

        .globl  stub_base
        .align  16
stub_base:
        .altmacro
        .set    i, 0
        .rept   256
        stub    %i
        .set    i, i + 1
        .endr
        .noaltmacro

/* struct trapframe (arch.h), low to high: pushal (edi..eax), gs, fs, es, ds,
   vector, error code, then what the CPU pushed. trap_dispatch may edit it;
   the IRET resumes whatever it says. A gate leaves DF as the interrupted
   code had it (DOS may be mid STD; REP MOVSB), and C code, and any thread
   the scheduler resumes from here, needs it clear: the IRET restores it. */
trap_common:
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs
        pushal
        cld
        movw    $0x10, %ax
        movw    %ax, %ds
        movw    %ax, %es
        pushl   %esp
        call    trap_dispatch
        addl    $4, %esp
        popal
        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        addl    $8, %esp
        iretl

/* void vm_enter(struct trapframe *tf): into the system VM, as if returning
   from a trap that came from it. */
        .globl  vm_enter
vm_enter:
        movl    4(%esp), %esp
        popal
        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        addl    $8, %esp
        iretl

/* void switch_to(u32 *save_esp, u32 new_esp): the scheduler's stack switch
   (core/sched.c). Saves the callee-saved registers on this stack, its ESP
   in *save_esp, and resumes the other stack where it called switch_to (or
   at thread_start for a new thread). */
        .globl  switch_to
switch_to:
        movl    4(%esp), %eax
        movl    8(%esp), %edx
        pushl   %ebp
        pushl   %ebx
        pushl   %esi
        pushl   %edi
        movl    %esp, (%eax)
        movl    %edx, %esp
        popl    %edi
        popl    %esi
        popl    %ebx
        popl    %ebp
        ret

/* The #DF task (a task gate in the IDT): report and stop. */
        .globl  df_entry
df_entry:
        pushfl
        andl    $~0x4000, (%esp)
        popfl
        call    double_fault
1:      cli
        hlt
        jmp     1b

/* void leave_to_loader(u32 code, u32 loader_cr3): back to GLOS.EXE's 16-bit
   code (38h:ret_off), which leaves protected mode. */
        .globl  leave_to_loader
leave_to_loader:
        cli
        movl    8(%esp), %ecx
        movl    4(%esp), %eax
        movl    %ecx, %cr3
        ljmp    *ret_farptr

/* u32 try_rd_cr4(u32 *v) / try_wr_cr4(u32 v): 0, or 1 + the vector of the
   fault (#UD on CPUs without CR4, #GP for an unsupported bit). */
        .globl  try_rd_cr4, try_wr_cr4
try_rd_cr4:
        movl    $9f, fixup_eip
        movl    %cr4, %eax
        movl    $0, fixup_eip
        movl    4(%esp), %edx
        movl    %eax, (%edx)
        xorl    %eax, %eax
        ret
try_wr_cr4:
        movl    4(%esp), %eax
        movl    $9f, fixup_eip
        movl    %eax, %cr4
        movl    $0, fixup_eip
        xorl    %eax, %eax
        ret
9:      movl    fixup_vec, %eax
        incl    %eax
        ret

/* u32 try_rd32(u32 addr, u32 *v) / try_wr8(u32 addr, u32 v): the same for
   memory (the gdb stub reads and writes through these). */
        .globl  try_rd32, try_wr8
try_rd32:
        movl    4(%esp), %edx
        movl    $8f, fixup_eip
        movl    (%edx), %eax
        movl    $0, fixup_eip
        movl    8(%esp), %edx
        movl    %eax, (%edx)
        xorl    %eax, %eax
        ret
try_wr8:
        movl    4(%esp), %edx
        movl    8(%esp), %eax
        movl    $8f, fixup_eip
        movb    %al, (%edx)
        movl    $0, fixup_eip
        xorl    %eax, %eax
        ret
8:      movl    fixup_vec, %eax
        incl    %eax
        ret

        .data
        .align  4
        .globl  fixup_eip, fixup_vec, ret_farptr
fixup_eip:      .long 0
fixup_vec:      .long 0
ret_farptr:     .long 0                 /* offset, then the selector */
                .word 0

        .section .note.GNU-stack, "", @progbits
