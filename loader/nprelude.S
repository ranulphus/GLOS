/* GLOSLDR.SYS's first 512 bytes (docs/v1/native-dos.md, native boot, NB1). GLOS's boot sector
   (kernel/disk/fdboot.S) loads the file whole at 0060:0000h and jumps there with BL the BIOS's number of the disk;
   after these 512 bytes the file is an .EXE, GLOS's loader built with no DOS under it (-dNATIVE). This makes that
   .EXE run where it lies, as DOS's EXEC would have made it run:

   - its relocations applied (its image starts at the header's end: its load segment is that paragraph's);
   - a PSP for it in the 256 bytes before its image (over the header, read by then): INT 20h at 0, the segment
     past its memory at 2 (its image and its minimum allocation), the rest zero; the kernel adopts it as the
     first program when GLOS's own DOS starts in the system VM (NB2);
   - its command tail, PSP:80h, from the 94 bytes at 01A0h of this sector (CMDLINE: an ASCIZ string, empty as
     built; tools/ndos/native-image.sh writes one there): the loader's command line, as GLOS.EXE's is under DOS
     (a folder for GLOS's files, \GLOS if none, and GLOS.EXE's flags);
   - DS and ES the PSP, SS:SP and CS:IP from the header, DL the disk;
   - the volume's first sector on the disk, for the loader, at 7000:0070h: the "hidden sectors" of the volume's
     parameters as GLOS's boot sector has them in memory when it enters here (it runs at 1FE0:7C00h with SS its
     segment and BP at its start; a BIOS that starts a USB stick as a floppy has made them 0 there, which is
     right for the drive it then is). FFFFFFFFh if something else loaded this.

   16-bit code, assembled once and linked as bytes (objcopy); it never runs again. Its code stays below 0700h:
   the PSP may begin at 0720h (a header of two paragraphs). */
    .code16
    .text
    .globl _start
_start:
    cli
    orl $-1, %ecx                       /* the volume's start, from the boot sector's parameters */
    movw %ss, %ax
    cmpw $0x1FE0, %ax
    jne 1f
    cmpw $0xAA55, %ss:0x1FE(%bp)
    jne 1f
    movl %ss:0x1C(%bp), %ecx
1:  movw $0x0060, %ax
    movw %ax, %ds
    movw $0x7000, %ax                   /* a stack away from everything: 7000:FFFEh (448 KB up) */
    movw %ax, %ss
    movw $0xFFFE, %sp
    movl %ecx, %ss:0x70
    sti
    cld
    movb %bl, %dl                       /* the disk, kept in DL to the end */
    pushw %dx                           /* COM1 at 115200 8N1, as the loader sets it, and a line on it */
    movw $0x3FB, %dx
    movb $0x80, %al
    outb %al, %dx
    movw $0x3F8, %dx
    movb $1, %al
    outb %al, %dx
    incw %dx
    xorb %al, %al
    outb %al, %dx
    movw $0x3FB, %dx
    movb $3, %al
    outb %al, %dx
    movw $(said - _start), %si
4:  lodsb
    orb %al, %al
    jz 6f
    movb %al, %ah
    movw $0x3FD, %dx
5:  inb %dx, %al
    testb $0x20, %al
    jz 5b
    movw $0x3F8, %dx
    movb %ah, %al
    outb %al, %dx
    jmp 4b
6:  popw %dx
    movw $0x0080, %ax                   /* the header: file offset 512, linear 0800h */
    movw %ax, %es
    cmpw $0x5A4D, %es:0                 /* "MZ" */
    jne bad
    movw %es:8, %bp                     /* header paragraphs */
    addw $0x0080, %bp                   /* BP: the load segment */
    movw %es:6, %cx                     /* relocations */
    movw %es:0x18, %si                  /* the table's offset in the header */
    jcxz relocated
reloc:
    movw %es:(%si), %di                 /* offset */
    movw %es:2(%si), %ax                /* segment, relative */
    addw %bp, %ax
    pushw %es
    movw %ax, %es
    addw %bp, %es:(%di)
    popw %es
    addw $4, %si
    loop reloc
relocated:
    /* the program's memory: its image's paragraphs (pages * 32, less the header's, the last page's part) and its
       minimum allocation; the segment past it */
    movw %es:4, %ax                     /* pages of 512 bytes */
    movw %es:2, %bx                     /* bytes in the last one (0: a whole page) */
    orw %bx, %bx
    jz 1f
    decw %ax
1:  movw $5, %cx
    shlw %cl, %ax                       /* pages as paragraphs */
    addw $15, %bx
    movw $4, %cx
    shrw %cl, %bx
    addw %bx, %ax                       /* + the last page's */
    subw %es:8, %ax                     /* - the header's */
    addw %es:0x0A, %ax                  /* + the minimum allocation */
    addw %bp, %ax                       /* AX: the segment past it */
    /* SS:SP and CS:IP, kept on the stack before the header is overwritten */
    movw %es:0x0E, %bx
    addw %bp, %bx
    pushw %bx                           /* SS */
    pushw %es:0x10                      /* SP */
    movw %es:0x16, %bx
    addw %bp, %bx
    pushw %bx                           /* CS */
    pushw %es:0x14                      /* IP */
    /* the command line, out of the PSP's way (the PSP may lie over this sector's end): to 7000:0000 */
    pushw %ax
    movw $0x7000, %ax
    movw %ax, %es
    movw $(cmdline - _start), %si
    xorw %di, %di
    movw $47, %cx
    rep movsw
    popw %ax
    /* the PSP: the paragraph 10h before the image */
    movw %bp, %bx
    subw $0x10, %bx
    movw %bx, %es
    pushw %ax
    xorw %di, %di
    xorw %ax, %ax
    movw $128, %cx
    rep stosw
    popw %ax
    movw $0x20CD, %es:0                 /* INT 20h */
    movw %ax, %es:2                     /* the segment past its memory */
    pushw %ds                           /* its tail: the length, the text, a CR */
    pushw %ax
    movw $0x7000, %ax
    movw %ax, %ds
    xorw %si, %si
    movw $0x81, %di
    xorw %cx, %cx
4:  lodsb
    orb %al, %al
    jz 5f
    stosb
    incw %cx
    cmpw $93, %cx
    jb 4b
5:  movb $0x0D, %es:(%di)
    movb %cl, %es:0x80
    popw %ax
    popw %ds
    popw %si                            /* IP */
    popw %di                            /* CS */
    popw %cx                            /* SP */
    popw %ax                            /* SS */
    cli
    movw %ax, %ss
    movw %cx, %sp
    sti
    movw %es, %ax
    movw %ax, %ds                       /* DS = ES = the PSP */
    pushw %di
    pushw %si
    lret                                /* (DL the disk) */
bad:
    movw $msg - _start, %si
2:  lodsb
    orb %al, %al
    jz 3f
    movb $0x0E, %ah
    xorw %bx, %bx
    int $0x10
    jmp 2b
3:  hlt
    jmp 3b
msg:
    .asciz "No loader\r\n"
said:
    .asciz "GLOS-BOOT step=prelude\r\n"
    .org 0x1A0
cmdline:                                /* CMDLINE: 93 bytes and their NUL */
    .org 510
    .byte 0x47, 0x4C                    /* ("GL": padding to 512 bytes) */
