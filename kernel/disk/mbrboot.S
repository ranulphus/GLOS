/* GLOS's boot code for a hard disk's first sector (kernel/disk/disk.h: mbr_boot). The BIOS loads the sector at
   0:7C00h and jumps there with the disk in DL. The code moves itself to 0:0600h, finds the active partition,
   reads its first sector to 0:7C00h (the BIOS's LBA call where it has one, else by the entry's cylinder, head
   and sector) and jumps to it, DL the disk and SI the entry, as DOS's boot sectors expect. 386 code: GLOS
   needs one. Linked into the kernel as bytes (mbr_code to mbr_code_end); never run there. */
    .section .rodata
    .code16
    .globl mbr_code, mbr_code_end
mbr_code:
    cli
    xorw %ax, %ax
    movw %ax, %ss
    movw $0x7C00, %sp
    movw %ax, %ds
    movw %ax, %es
    sti
    cld
    movw $0x7C00, %si
    movw $0x0600, %di
    movw $256, %cx
    rep movsw
    ljmp $0, $(0x0600 + 1f - mbr_code)
1:  movw $(0x0600 + 0x1BE), %si
    movb $4, %cl
2:  cmpb $0x80, (%si)
    je 3f
    addw $16, %si
    loop 2b
    movw $(0x0600 + msg_none - mbr_code), %si
    jmp 8f
3:  movw %si, %bp
    movb %dl, (0x0600 + drive - mbr_code)
    movb $0x41, %ah                     /* the BIOS's extensions? */
    movw $0x55AA, %bx
    int $0x13
    jc 4f
    cmpw $0xAA55, %bx
    jne 4f
    testb $1, %cl
    jz 4f
    movw $(0x0600 + packet - mbr_code), %si
    movl 8(%bp), %eax
    movl %eax, 8(%si)
    movb (0x0600 + drive - mbr_code), %dl
    movb $0x42, %ah
    int $0x13
    jnc 5f
4:  movb (0x0600 + drive - mbr_code), %dl      /* by cylinder, head and sector */
    movb 1(%bp), %dh
    movw 2(%bp), %cx
    movw $0x7C00, %bx
    movw $0x0201, %ax
    int $0x13
    jc 6f
5:  cmpw $0xAA55, 0x7DFE
    jne 7f
    movw %bp, %si
    movb (0x0600 + drive - mbr_code), %dl
    ljmp $0, $0x7C00
6:  movw $(0x0600 + msg_read - mbr_code), %si
    jmp 8f
7:  movw $(0x0600 + msg_system - mbr_code), %si
8:  lodsb
    testb %al, %al
    jz 9f
    movb $0x0E, %ah
    movw $7, %bx
    int $0x10
    jmp 8b
9:  hlt
    jmp 9b
drive:
    .byte 0
packet:
    .byte 0x10, 0
    .word 1
    .word 0x7C00, 0
    .long 0, 0
msg_none:
    .asciz "No partition to start from\r\n"
msg_read:
    .asciz "The disk can't be read\r\n"
msg_system:
    .asciz "No system on the disk's partition\r\n"
mbr_code_end:
    .section .note.GNU-stack, "", @progbits
