/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 non-secure image (#590, docs/plans/590.md), preprocessed with the
 * board's tz_map_<board>.h as tz_s.ld is. It links against the secure link's
 * import library, whose symbols are the SG veneers' addresses in the NSC
 * region: the only secure addresses this image names.
 */

ENTRY(tz_ns_reset)

MEMORY
{
    NS_CODE (rx)  : ORIGIN = TZ_NS_CODE_BASE, LENGTH = TZ_NS_CODE_SIZE
    NS_RAM  (rwx) : ORIGIN = TZ_NS_RAM_BASE,  LENGTH = TZ_NS_RAM_SIZE
}

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > NS_CODE

    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > NS_CODE

    .data :
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > NS_RAM AT > NS_CODE
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > NS_RAM

    __stack = ORIGIN(NS_RAM) + LENGTH(NS_RAM);

    /* Where QEMU loads the raw image (the Secure alias of the same RAM on
     * mps2), and the Secure SRAM the QEMU case expects the fault at. */
    __tz_load  = TZ_NS_CODE_LOAD;
    __tz_s_ram = TZ_S_RAM_BASE;

    __tz_code_used = __data_load + SIZEOF(.data) - ORIGIN(NS_CODE);
    __tz_code_size = LENGTH(NS_CODE);
    __tz_ram_used  = __bss_end - ORIGIN(NS_RAM);
    __tz_ram_size  = LENGTH(NS_RAM);
}
