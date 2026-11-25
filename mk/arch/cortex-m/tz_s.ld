/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 secure image (#590). Not read by ld as it is:
 * mk/arch/cortex-m-targets.mk runs it through the C preprocessor with the
 * board's src/hal/cortex-m/tz/tz_map_<board>.h, so every address below is
 * that header's, as the SAU table's are.
 *
 * The NSC region holds .gnu.sgstubs, the SG veneers the linker writes for
 * each cmse_nonsecure_entry function (--cmse-implib); nothing else goes in
 * it, so no other secure code is callable from the Non-secure world.
 */

ENTRY(tz_s_reset)

MEMORY
{
    S_CODE (rx)  : ORIGIN = TZ_S_CODE_BASE, LENGTH = TZ_S_CODE_SIZE
    NSC    (rx)  : ORIGIN = TZ_NSC_BASE,    LENGTH = TZ_NSC_SIZE
    S_RAM  (rwx) : ORIGIN = TZ_S_RAM_BASE,  LENGTH = TZ_S_RAM_SIZE
}

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > S_CODE

    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > S_CODE

    .data :
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > S_RAM AT > S_CODE
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > S_RAM

    /* ld sizes the veneers before it lays out the script and refuses a
     * .gnu.sgstubs whose address only the script gives, so the make passes
     * TZ_NSC_BASE as --section-start as well (cortex-m-targets.mk). */
    .gnu.sgstubs :
    {
        KEEP(*(.gnu.sgstubs*))
    } > NSC

    __stack = ORIGIN(S_RAM) + LENGTH(S_RAM);

    /* Where QEMU loads the raw image (cortex-m-qemu-tz-run). */
    __tz_load = TZ_S_CODE_BASE;

    /* What tz-size-check reads. */
    __tz_code_used = __data_load + SIZEOF(.data) - ORIGIN(S_CODE);
    __tz_code_size = LENGTH(S_CODE);
    __tz_nsc_used  = SIZEOF(.gnu.sgstubs);
    __tz_nsc_size  = LENGTH(NSC);
    __tz_ram_used  = __bss_end - ORIGIN(S_RAM);
    __tz_ram_size  = LENGTH(S_RAM);

    ASSERT(__tz_nsc_used > 0, "no SG veneer in the NSC region: the secure image exports nothing")
}
