ENTRY(_start)

SECTIONS {
    . = 0x00000000; /* User program dimuat di virtual address 0x0 */
    
    .text ALIGN(4): {
        *crt0.o(.text)
        *(.text)
    }

    .data ALIGN(4): {
        *(.data)
    }

    .bss ALIGN(4): {
        *(COMMON)
        *(.bss)
    }

    .rodata ALIGN(4): {
        *(.rodata*)
    }
    _linker_user_program_end = .;
    ASSERT ((_linker_user_program_end <= 1 * 1024 * 1024), "Error: User program linking result is more than 1 MiB")
}