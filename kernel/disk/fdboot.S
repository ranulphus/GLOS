/* GLOS's boot code for a FAT12 or FAT16 volume's first sector (kernel/disk/disk.h: fat_boot): it starts
   FreeDOS's kernel, as FreeDOS's SYS would have the volume do, so that GLOS makes a disk a PC starts from
   with no tool of the DOS's (docs/v1/shell.md, step S0). Written from what FreeDOS's kernel asks of whatever
   loads it, not from FreeDOS's boot sector:

   - KERNEL.SYS, from the root folder, whole, at 0060:0000h;
   - entered there with BL the BIOS's number of the disk (DL too), a stack, and no "CL" word at SS:BP-14h
     (where newer kernels look for a command line).

   The BIOS (or the first sector's code, kernel/disk/mbrboot.S) enters at 0:7C00h with the disk in DL. The
   code moves itself to 1FE0:7C00h (the kernel's load would run over it otherwise), with its stack below
   itself and its buffers above; reads by the BIOS's LBA call on a hard disk where there is one, else by
   cylinder, head and sector from the volume's own geometry. 386 code: GLOS needs one.

   One body, assembled once for each kind of FAT; each is the code from offset 62 of the sector (after the
   volume's parameters, which fatfmt.c writes) to its end. Linked into the kernel as bytes; never run there. */
#define SEG      0x1FE0                 /* where the code runs */
#define FATSTART 0x7E00                 /* (in SEG) the first FAT's sector on the disk, a long */
#define DATASTART 0x7E04                /* the sector of cluster 2, a long */
#define FATHAVE  0x7E08                 /* FAT16: the FAT's sector in FATBUF, a long */
#define DIRBUF   0x8000                 /* a sector of the root folder */
#define FATBUF   0x8200                 /* FAT12: the whole FAT (12 sectors at most); FAT16: one sector of it */

    .section .rodata
    .code16

    .macro bootcode fat
0:  cli
    xorw %ax, %ax
    movw %ax, %ss
    movw $0x7C00, %sp
    movw %ax, %ds
    sti
    cld
    movw $SEG, %ax
    movw %ax, %es
    movw %sp, %si
    movw %sp, %di
    movw $256, %cx
    rep movsw
    ljmp $SEG, $(0x7C00 + 62 + 1f - 0b)
1:  movw %ax, %ds
    movw %ax, %ss
    movw $0x7C00, %sp
    movw %sp, %bp
    movb %dl, 0x24(%bp)                 /* the disk, where the volume's parameters keep it */
    movzwl 0x0E(%bp), %eax              /* the reserved sectors ... */
    addl 0x1C(%bp), %eax                /* ... after the sectors before the volume: the first FAT */
    movl %eax, FATSTART
    .if \fat == 12
    movw 0x16(%bp), %cx                 /* FAT12: the whole FAT read now */
    movw $FATBUF, %bx
2:  call 9f
    incl %eax
    addw $512, %bx
    loop 2b
    movl FATSTART, %eax
    .else
    movl %eax, FATHAVE                  /* (no sector of it yet: the first FAT's start is not a FAT sector index) */
    decl FATHAVE
    .endif
    movzwl 0x16(%bp), %edx
    movzbl 0x10(%bp), %ecx
    imull %ecx, %edx
    addl %edx, %eax                     /* the root folder */
    movw 0x11(%bp), %cx
    addw $15, %cx
    shrw $4, %cx                        /* its sectors */
    movzwl %cx, %edx
    addl %eax, %edx
    movl %edx, DATASTART
    movw $DIRBUF, %bx
2:  call 9f                             /* a sector of it */
    movw %bx, %di
3:  cmpb $0, (%di)
    je 8f                               /* the folder's end: no kernel */
    pushw %cx
    pushw %di
    movw $(0x7C00 + 62 + 7f - 0b), %si
    movw $11, %cx
    repe cmpsb
    popw %di
    popw %cx
    je 4f
    addw $32, %di
    cmpw $(DIRBUF + 512), %di
    jb 3b
    incl %eax
    loop 2b
    jmp 8f
4:  movw 0x1A(%di), %ax                 /* its first cluster */
    pushw $0x0060
    popw %es
5:  cmpw $2, %ax                        /* a cluster of the file, to ES:0 */
    jb 6f
    .if \fat == 12
    cmpw $0x0FF8, %ax
    .else
    cmpw $0xFFF8, %ax
    .endif
    jae 6f
    pushw %ax
    movzwl %ax, %eax
    subl $2, %eax
    movzbl 0x0D(%bp), %ecx
    imull %ecx, %eax
    addl DATASTART, %eax
    xorw %bx, %bx
2:  call 9f
    incl %eax
    movw %es, %dx
    addw $0x20, %dx
    movw %dx, %es
    loop 2b
    popw %ax
    .if \fat == 12
    movw %ax, %si                       /* the next: 12 bits at one and a half times the cluster */
    shrw $1, %si
    addw %ax, %si
    movw FATBUF(%si), %dx
    testb $1, %al
    jz 2f
    shrw $4, %dx
2:  andw $0x0FFF, %dx
    movw %dx, %ax
    .else
    pushw %es                           /* the next: 16 bits, from the FAT's sector that holds them */
    pushw %ds
    popw %es
    movw %ax, %si
    movzbl %ah, %eax
    addl FATSTART, %eax
    cmpl FATHAVE, %eax
    je 2f
    movl %eax, FATHAVE
    movw $FATBUF, %bx
    call 9f
2:  andw $0x00FF, %si
    shlw $1, %si
    movw FATBUF(%si), %ax
    popw %es
    .endif
    jmp 5b
6:  movb 0x24(%bp), %bl                 /* the kernel is in: enter it */
    movb %bl, %dl
    movw $0, -0x14(%bp)
    ljmp $0x0060, $0

7:  .ascii "KERNEL  SYS"
    .asciz "No DOS here. Press a key.\r\n"

8:  movw $(0x7C00 + 62 + 7b + 11 - 0b), %si     /* no kernel, or the disk can't be read: say so, then start again */
2:  lodsb
    testb %al, %al
    jz 3f
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp 2b
3:  xorw %ax, %ax
    int $0x16
    int $0x19

/* The disk's sector EAX to ES:BX; every register kept. */
9:  pushal
    movb 0x24(%bp), %dl
    testb %dl, %dl
    jns 3f                              /* a floppy: by cylinder, head and sector */
    pushl $0
    pushl %eax
    pushw %es
    pushw %bx
    pushw $1
    pushw $16
    movw %sp, %si
    movb $0x42, %ah
    int $0x13
    leaw 16(%si), %sp                   /* (flags kept) */
    jnc 4f
    popal                               /* no LBA call in this BIOS, or it failed: the other way */
    pushal
3:  movw $3, %di                        /* (a floppy's motor: three tries) */
2:  pushl %eax
    xorl %edx, %edx
    movzwl 0x18(%bp), %ecx
    divl %ecx
    incw %dx
    pushw %dx                           /* the sector */
    xorl %edx, %edx
    movzwl 0x1A(%bp), %ecx
    divl %ecx                           /* the cylinder, and the head left over */
    movb %dl, %dh
    popw %cx
    movb %al, %ch
    shlb $6, %ah
    orb %ah, %cl
    movb 0x24(%bp), %dl
    movw $0x0201, %ax
    int $0x13
    popl %eax
    jnc 4f
    pushw %ax
    xorw %ax, %ax                       /* the drive reset, and again */
    int $0x13
    popw %ax
    decw %di
    jnz 2b
    jmp 8b
4:  popal
    ret
    .endm

    .globl fdboot12_code, fdboot12_end, fdboot16_code, fdboot16_end
fdboot12_code:
    bootcode 12
fdboot12_end:
fdboot16_code:
    bootcode 16
fdboot16_end:
