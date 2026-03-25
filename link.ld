OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = 0x00200000;
    _start = .;
    _load_start = .;
    .text : { *(.text.boot) *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : {
        __bss_start = .;
        *(.sbss) *(.bss)
        __bss_stop = .;
    }
    _load_end = .;
    . = ALIGN(16);
    . = . + 0x4000;
    _end = .;
}
