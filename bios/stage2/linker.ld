/*
 * SakuruBoot Stage 2 linker script.
 * Links at base address 0x7E00 (immediately after the MBR at 0x7C00).
 * entry.asm (.text.entry) must come first to ensure the 16-bit
 * initialization code is at the very start of the image.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS {
    . = 0x0000000000007E00;

    /* Stage 2 entry (must be first — real-mode startup code) */
    .text.entry : {
        *(.text.entry)
    }

    /* Rest of code */
    .text : {
        *(.text*)
    }

    .rodata ALIGN(16) : {
        *(.rodata*)
    }

    .data ALIGN(16) : {
        *(.data*)
    }

    .bss ALIGN(16) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment*)
    }
}
