/* The partition's first sector on the MBR boot code's test disk (tests/host/mbrdisk.c; Loop A's floppy suite,
   mbr_case): entered as a DOS boot sector is (0:7C00h, DL the disk, DS:SI the partition's entry), it says on
   COM1 what it was given and stops. Linked into mbrdisk as bytes. */
    .section .rodata
    .code16
    .globl stub_code, stub_code_end
stub_code:
    cli
    movb %dl, %bl                       /* the disk */
    movb (%si), %bh                     /* the entry's first byte: 80h, the active partition */
    movw 8(%si), %cx                    /* ... and its start, the low word */
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %ss
    movw $0x7C00, %sp
    movw $0x3FB, %dx                    /* COM1: 8 bits, no parity (the speed as the BIOS left it) */
    movb $3, %al
    outb %al, %dx
    movw $(0x7C00 + m1 - stub_code), %si
    call say
    movb %bl, %al
    call hex
    movw $(0x7C00 + m2 - stub_code), %si
    call say
    movb %bh, %al
    call hex
    movw $(0x7C00 + m3 - stub_code), %si
    call say
    movb %ch, %al
    call hex
    movb %cl, %al
    call hex
    movw $(0x7C00 + m4 - stub_code), %si
    call say
1:  hlt
    jmp 1b

say:
    lodsb
    testb %al, %al
    jz 9f
    call putc
    jmp say
9:  ret

hex:
    pushw %ax
    shrb $4, %al
    call nib
    popw %ax
    andb $15, %al
nib:
    addb $'0', %al
    cmpb $'9', %al
    jbe putc
    addb $7, %al
putc:
    movb %al, %ah
2:  movw $0x3FD, %dx
    inb %dx, %al
    testb $0x20, %al
    jz 2b
    movw $0x3F8, %dx
    movb %ah, %al
    outb %al, %dx
    ret

m1: .asciz "HX-START mbr\r\nHX-MBR-OK dl="
m2: .asciz " active="
m3: .asciz " start="
m4: .asciz "\r\nHX-DONE 0\r\n"
stub_code_end:
