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
/* The way out of every trap, and into user mode from a frame the kernel
   built (vm_enter, nest_enter, frame_resume). A ring-3 return to a 16-bit
   stack goes through espfix (supervisor.md §6.3): trap_dispatch has set the
   espfix selector's base and espfix_ptr so that SS:ESP addresses this same
   frame with ESP[31:16] already the client's. vm_user_exit() gives a frame
   to user mode its IOPL and IF (a direct-mode session's, v86.c). */
trap_exit:
        pushl   %esp
        call    vm_user_exit
        addl    $4, %esp
        popal
        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        addl    $8, %esp
        cmpl    $0, %ss:espfix_pending
        jne     1f
        iretl
1:      movl    $0, %ss:espfix_pending
        lss     %ss:espfix_ptr, %esp
        iretl

/* u32 nest_enter(struct trapframe *f, u32 *save): into user mode from frame
   f, which lies below this call's own stack, so that what the caller has on
   the stack survives; returns the value given to nest_leave() by a trap
   handler further down (kernel/dpmi/rmcall.c). */
        .globl  nest_enter, nest_leave, frame_resume
nest_enter:
        movl    4(%esp), %eax
        movl    8(%esp), %edx
        pushl   %ebp
        pushl   %ebx
        pushl   %esi
        pushl   %edi
        movl    %esp, (%edx)
        movl    %eax, %esp
        jmp     trap_exit

/* void nest_leave(u32 saved_esp, u32 value): back into nest_enter's caller. */
nest_leave:
        movl    8(%esp), %eax
        movl    4(%esp), %esp
        popl    %edi
        popl    %esi
        popl    %ebx
        popl    %ebp
        ret

/* void frame_resume(struct trapframe *f): into user mode from f, dropping
   everything on the kernel stack above it (a nested level that ended). */
frame_resume:
        movl    4(%esp), %esp
        jmp     trap_exit

/* void vm_enter(struct trapframe *tf): into the system VM, as if returning
   from a trap that came from it. */
        .globl  vm_enter
vm_enter:
        movl    4(%esp), %esp
        jmp     trap_exit

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

/* u32 ucopy(void *dst, const void *src, u32 n): a copy to or from a DPMI
   client's memory, which may not be mapped: 0, or 1 + the fault's vector. */
        .globl  ucopy
ucopy:
        pushl   %esi
        pushl   %edi
        movl    12(%esp), %edi
        movl    16(%esp), %esi
        movl    20(%esp), %ecx
        movl    $7f, fixup_eip
        rep movsb
        movl    $0, fixup_eip
        xorl    %eax, %eax
        popl    %edi
        popl    %esi
        ret
7:      movl    fixup_vec, %eax
        incl    %eax
        popl    %edi
        popl    %esi
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
        .globl  fixup_eip, fixup_vec, ret_farptr, espfix_pending, espfix_ptr
fixup_eip:      .long 0
fixup_vec:      .long 0
espfix_pending: .long 0
espfix_ptr:     .long 0                 /* ESP, then SS (the espfix selector) */
                .word 0
                .word 0
ret_farptr:     .long 0                 /* offset, then the selector */
                .word 0

        .section .note.GNU-stack, "", @progbits
