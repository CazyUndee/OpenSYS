ENTRY(_start)

SECTIONS
{
    /* Load kernel at 2MB (leaves room for bootloader) */
    . = 0x200000;

    /* Boot section first (contains multiboot header) */
    .boot :
    {
        *(.multiboot)
    }

    /* 64-bit code */
    .text :
    {
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialized data */
    .data :
    {
        *(.data)
        *(.data.*)
    }

    /* BSS (uninitialized data) */
    .bss :
    {
        bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        bss_end = .;
    }

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }

    kernel_end = .;
}
