OUTPUT_ARCH("riscv")
ENTRY(_entry)

SECTIONS
{
    . = 0x80000000;
    
    .text : {
        *(.text._entry)
        *(.text .text.*)
        . = ALIGN(0x1000);
        *(trampsec)
        . = ALIGN(0x1000);
        PROVIDE(etext = .);
    }

    .rodata : {
        /* Read Only Data */
        . = ALIGN(16);
        *(.rodata .rodata.*)
    }

    .data : {
        . = ALIGN(16);
        *(.data .data.*)
    }

    .bss : {
        /*16 Byte*/
        . = ALIGN(16);
        sbss = .;

        *(.sbss .sbss.*)

        . = ALIGN(16);
        *(.bss .bss.*)

        ebss = .;
    }

    . = ALIGN(4096);
    . = . + 4096;
    stack_top = .;

    PROVIDE(end = .);

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}