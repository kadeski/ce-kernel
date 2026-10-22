/*
 * Bare ROM layout (4 MB flash image):
 *   0x000000  reset vector
 *   0x020000  model header read by CEmu
 *   0x030000  code, read-only data, initial values of .data
 * RAM 0xD00000-0xD3FFFF holds .data, .bss, free memory and the stack;
 * 0xD40000 onward is VRAM (the framebuffer).
 */

MEMORY
{
    flash (rx) : ORIGIN = 0x000000, LENGTH = 0x400000
    ram  (rwx) : ORIGIN = 0xD00000, LENGTH = 0x040000
}

__stack_top = 0xD40000;
STACK_SIZE = 0x1000;

SECTIONS
{
    .reset 0x000000 : { KEEP(*(.reset)) } >flash
    .cert  0x020000 : { KEEP(*(.cert)) } >flash

    .text 0x030000 :
    {
        ___image_start = .;
        *(.text.__rom_start)
        *(.text .text.*)
    } >flash

    .rodata : { *(.rodata .rodata.*) } >flash

    .data :
    {
        *(.data .data.*)
    } >ram AT>flash
    ___data_vma = ADDR(.data);
    ___data_lma = LOADADDR(.data);
    ___data_len = SIZEOF(.data);
    ___image_end = LOADADDR(.data) + SIZEOF(.data);

    .bss (NOLOAD) :
    {
        ___bss_low = .;
        *(.bss .bss.*)
        *(COMMON)
        ___bss_high = .;
    } >ram
    ___bss_len = ___bss_high - ___bss_low;

    ___heap_low = ___bss_high;
    ___heap_high = __stack_top - STACK_SIZE;
    ASSERT(___heap_low <= ___heap_high, "Error: .bss overlaps the stack")

    /DISCARD/ : { *(.comment) *(.init_array*) *(.fini_array*) }
}
