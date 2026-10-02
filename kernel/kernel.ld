/* GLOSK.BIN: one flat image linked at KERNEL_LINK (supervisor.md §4). The
   loader copies the file to its physical pages and zeroes up to __kernel_end. */
ENTRY(_start)
SECTIONS
{
    . = 0xC0100000;
    __kernel_start = .;
    .text : { *(.text.head) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) . = ALIGN(16); }     /* padded, so the file covers it */
    __file_end = .;
    .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
    . = ALIGN(4096);
    __kernel_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
