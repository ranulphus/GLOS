/* The kernel's entry: the header GLOS.EXE reads, then the first instructions.
   The loader arrives with paging on (its page tables), CS=08h, DS=ES=SS=10h,
   ESI = the struct bootinfo (linear, identity-mapped). */
        .code32
        .section .text.head, "ax"
        .globl  _start
_start:
        .long   0x4B4F4C47              /* "GLOK" (KERNEL_MAGIC) */
        .long   entry - _start
        .long   __file_end - _start
        .long   __kernel_end - _start
entry:
        cld
        movl    $boot_stack_top, %esp
        pushl   %esi
        call    kmain                   /* does not return: leave_to_loader() */
1:      cli
        hlt
        jmp     1b

        .bss
        .align  16
        .globl  boot_stack, boot_stack_top
boot_stack:
        .space  16384
boot_stack_top:

        .section .note.GNU-stack, "", @progbits
