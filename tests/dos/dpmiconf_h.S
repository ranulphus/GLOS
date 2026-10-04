/* DPMICONF-32's handlers (tests/dos/dpmiconf.c; M4b): exception, IRQ, INT
 * 1Ch and real-mode callback handlers, and the code that faults for them.
 * Handlers start with DS as whatever was interrupted (a callback's DS is
 * the real-mode stack), so what they share with the C side sits here in
 * .text, read through CS as DJGPP's own handlers do (exceptn.S), and is
 * written through hd_ds, a copy of the program's DS alias. */
        .text

        .globl  _hd_ds, _old8, _old1c, _old75, _irq8_mode
        .globl  _irq8_hits, _irq8_vif, _irq8_ss, _irq8_base, _i1c_hits, _irq13_hits, _mf_hits
        .globl  _exc_hits, _exc_err, _exc_eip, _exc_cs, _exc_flags, _exc_esp, _exc_ss, _exc_hss
        .globl  _exc_skip, _exc_ebx, _exc_new_eip, _exc_new_esp
        .globl  _x10_hits, _x10_eip, _x10_cs, _x10_info, _x10_flags, _x10_es, _x10_ds, _x10_cr2, _x10_pte
        .globl  _rmcb_hits, _rmcb_eax, _rmcb_sp, _rmcb_ss_base
        .balign 4
_hd_ds:         .word   0, 0
_old8:          .long   0
                .word   0, 0
_old1c:         .long   0
                .word   0, 0
_old75:         .long   0
                .word   0, 0
_irq8_mode:     .long   0               /* 0: chain to the old handler; 1: EOI and IRET without STI */
_irq8_hits:     .long   0
_irq8_vif:      .long   0               /* INT 31h 0902h inside the handler */
_irq8_ss:       .long   0
_irq8_base:     .long   0               /* INT 31h 0006h on our DS, inside the handler */
_i1c_hits:      .long   0
_irq13_hits:    .long   0
_mf_hits:       .long   0
_exc_hits:      .long   0
_exc_err:       .long   0
_exc_eip:       .long   0
_exc_cs:        .long   0
_exc_flags:     .long   0
_exc_esp:       .long   0
_exc_ss:        .long   0
_exc_hss:       .long   0               /* the handler's own SS */
_exc_skip:      .long   0               /* bytes to add to EIP in the frame */
_exc_ebx:       .long   0               /* EBX the handler leaves (the resumed code keeps it) */
_exc_new_eip:   .long   0               /* if set: EIP for the frame */
_exc_new_esp:   .long   0               /* if set: ESP for the frame */
_x10_hits:      .long   0
_x10_eip:       .long   0
_x10_cs:        .long   0
_x10_info:      .long   0
_x10_flags:     .long   0
_x10_es:        .long   0
_x10_ds:        .long   0
_x10_cr2:       .long   0
_x10_pte:       .long   0
_rmcb_hits:     .long   0
_rmcb_eax:      .long   0
_rmcb_sp:       .long   0
_rmcb_ss_base:  .long   0

/* ---- exceptions: the 0.9 frame at ESP (return CS:EIP, error, CS:EIP,
   EFLAGS, SS:ESP); the handler records it, edits it, sets EBX, RETFs */
        .globl  _exc_handler
_exc_handler:
        pushl   %ds
        pushl   %eax
        movw    %cs:_hd_ds, %ax
        movw    %ax, %ds
        incl    _exc_hits
        movl    16(%esp), %eax
        movl    %eax, _exc_err
        movl    20(%esp), %eax
        movl    %eax, _exc_eip
        movl    24(%esp), %eax
        movzwl  %ax, %eax
        movl    %eax, _exc_cs
        movl    28(%esp), %eax
        movl    %eax, _exc_flags
        movl    32(%esp), %eax
        movl    %eax, _exc_esp
        movl    36(%esp), %eax
        movzwl  %ax, %eax
        movl    %eax, _exc_ss
        movw    %ss, %ax
        movzwl  %ax, %eax
        movl    %eax, _exc_hss
        movl    _exc_skip, %eax
        addl    %eax, 20(%esp)
        movl    _exc_new_eip, %eax
        testl   %eax, %eax
        jz      1f
        movl    %eax, 20(%esp)
1:      movl    _exc_new_esp, %eax
        testl   %eax, %eax
        jz      2f
        movl    %eax, 32(%esp)
2:      movl    _exc_ebx, %ebx
        popl    %eax
        popl    %ds
        lret

/* The 1.0 frame (0212h) at ESP+20h: return address, error, EIP, CS and
   info bits, EFLAGS, ESP, SS, ES, DS, FS, GS, CR2, PTE. The handler skips
   the instruction in both frames. */
        .globl  _x10_handler
_x10_handler:
        pushl   %ds
        pushl   %eax
        movw    %cs:_hd_ds, %ax
        movw    %ax, %ds
        incl    _x10_hits
        movl    0x2C+8(%esp), %eax
        movl    %eax, _x10_eip
        movzwl  0x30+8(%esp), %eax
        movl    %eax, _x10_cs
        movzwl  0x32+8(%esp), %eax
        movl    %eax, _x10_info
        movl    0x34+8(%esp), %eax
        movl    %eax, _x10_flags
        movzwl  0x40+8(%esp), %eax
        movl    %eax, _x10_es
        movzwl  0x44+8(%esp), %eax
        movl    %eax, _x10_ds
        movl    0x50+8(%esp), %eax
        movl    %eax, _x10_cr2
        movl    0x54+8(%esp), %eax
        movl    %eax, _x10_pte
        movl    _exc_skip, %eax
        addl    %eax, 0x0C+8(%esp)
        addl    %eax, 0x2C+8(%esp)
        popl    %eax
        popl    %ds
        lret

/* unsigned fault_gp(unsigned ebx): a write through CS is #GP(0) on any
   host (HDPMI32i emulates MOV from CR0, so not that); the handler skips it
   (3 bytes) and sets EBX, which comes back. */
        .globl  _fault_gp, _gp_insn
_fault_gp:
        pushl   %ebx
        movl    8(%esp), %ebx
        movl    %esp, _exc_esp_before
        movl    $_exc_saved_esp, %eax
_gp_insn:
        movl    %eax, %cs:(%eax)        /* 2E 89 00 */
        movl    %ebx, %eax
        popl    %ebx
        ret

/* unsigned fault_pf(unsigned sel, unsigned off): a 3-byte read at
   sel:off (FS); the handler skips it. Returns EAX (0 if skipped). */
        .globl  _fault_pf, _pf_insn
_fault_pf:
        pushl   %ebx
        pushl   %fs
        movl    12(%esp), %eax
        movw    %ax, %fs
        movl    16(%esp), %ebx
        xorl    %eax, %eax
_pf_insn:
        movl    %fs:(%ebx), %eax        /* 64 8B 03 */
        popl    %fs
        popl    %ebx
        ret

/* unsigned fault_resume(void): #GP, and the handler sends the code on to
   exc_resume, on the stack _exc_new_esp says; returns the ESP it got. */
        .globl  _fault_resume, _exc_resume
_fault_resume:
        pushl   %ebp
        movl    %esp, _exc_saved_esp
        movl    $_exc_saved_esp, %eax
        movl    %eax, %cs:(%eax)
        ud2
_exc_resume:
        movl    %esp, %eax
        movl    _exc_saved_esp, %esp
        popl    %ebp
        ret

        .globl  _exc_esp_before, _exc_saved_esp
_exc_esp_before: .long  0
_exc_saved_esp:  .long  0

/* ---- IRQ0 (INT 8): count, check INT 31h works inside (0902h, 0006h),
   then chain or EOI and IRET without STI (DJGPP's timer handler does) */
        .globl  _irq8_handler
_irq8_handler:
        pushl   %ds
        pushal
        movw    %cs:_hd_ds, %ax
        movw    %ax, %ds
        incl    _irq8_hits
        movw    %ss, %ax
        movzwl  %ax, %eax
        movl    %eax, _irq8_ss
        movw    $0x0902, %ax
        int     $0x31
        movzbl  %al, %eax
        movl    %eax, _irq8_vif
        movw    $0x0006, %ax
        movw    %cs:_hd_ds, %bx
        int     $0x31
        shll    $16, %ecx
        movw    %dx, %cx
        movl    %ecx, _irq8_base
        popal
        popl    %ds
        testb   $1, %cs:_irq8_mode
        jnz     1f
        ljmp    *%cs:_old8
1:      pushl   %eax
        movb    $0x20, %al
        outb    %al, $0x20
        popl    %eax
        iret

/* INT 1Ch, passed up from the BIOS's tick in real mode: count, chain. */
        .globl  _i1c_handler
_i1c_handler:
        pushl   %ds
        pushl   %eax
        movw    %cs:_hd_ds, %ax
        movw    %ax, %ds
        incl    _i1c_hits
        popl    %eax
        popl    %ds
        ljmp    *%cs:_old1c

/* IRQ13 (INT 75h), an FPU error with CR0.NE clear: as DJGPP's handler
   (port F0h, EOI both PICs), then FNCLEX, count, IRET. */
        .globl  _irq13_handler
_irq13_handler:
        pushl   %ds
        pushl   %eax
        xorb    %al, %al
        outb    %al, $0xF0
        movb    $0x20, %al
        outb    %al, $0xA0
        outb    %al, $0x20
        fnclex
        movw    %cs:_hd_ds, %ax
        movw    %ax, %ds
        incl    _irq13_hits
        popl    %eax
        popl    %ds
        iret

/* #MF (exception 10h), the same error with CR0.NE set: FNCLEX and return
   to the instruction, which now runs. */
        .globl  _mf_handler
_mf_handler:
        pushl   %ds
        pushl   %eax
        fnclex
        movw    %cs:_hd_ds, %ax
        movw    %ax, %ds
        incl    _mf_hits
        popl    %eax
        popl    %ds
        lret

/* void fpu_divzero(void): 1/0 with zero-divide unmasked; the FWAIT after
   it is where the error arrives (as IRQ13 or #MF). */
        .globl  _fpu_divzero
_fpu_divzero:
        subl    $4, %esp
        fnstcw  (%esp)
        movw    (%esp), %ax
        movw    %ax, 2(%esp)
        andw    $0xFFFB, (%esp)         /* ZM off: zero-divide unmasked */
        fldcw   (%esp)
        fld1
        fldz
        fdivrp  %st, %st(1)
        fwait
        fstp    %st(0)
        fwait
        movw    2(%esp), %ax
        movw    %ax, (%esp)
        fnclex
        fldcw   (%esp)
        addl    $4, %esp
        ret

/* ---- real-mode callbacks: DS:ESI the real-mode SS:SP, ES:EDI the
   structure. rmcb_far returns as a RETF would, rmcb_int as an IRET. */
        .globl  _rmcb_far, _rmcb_int
_rmcb_far:
        cld
        movl    %esi, %ecx
        lodsl                           /* the caller's IP and CS */
        movl    %eax, %es:0x2A(%edi)
        addw    $4, %es:0x2E(%edi)
        movl    %es:0x1C(%edi), %eax
        movl    $0xBEEF, %es:0x10(%edi) /* EBX for the caller */
        pushl   %eax
        movw    %ds, %ax
        movw    %cs:_hd_ds, %dx
        movw    %dx, %ds
        movzwl  %ax, %eax
        movl    %eax, _rmcb_ss_base     /* (the selector, for the C side to look up) */
        movl    %ecx, _rmcb_sp
        popl    %eax
        movl    %eax, _rmcb_eax
        incl    _rmcb_hits
        iret

_rmcb_int:
        cld
        lodsl                           /* IP and CS */
        movl    %eax, %es:0x2A(%edi)
        lodsw                           /* FLAGS */
        movw    %ax, %es:0x20(%edi)
        addw    $6, %es:0x2E(%edi)
        movl    $0xCAFE, %es:0x18(%edi) /* ECX for the caller */
        movw    %cs:_hd_ds, %ax
        movw    %ax, %ds
        incl    _rmcb_hits
        iret
