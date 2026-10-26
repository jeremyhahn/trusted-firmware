/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's images (#17): mps2-tz.ld's layout, with the vector table
 * made long enough for this SoC's interrupt numbers.
 *
 * No MEMORY block and no address: mk/arch/cortex-m.mk passes every one as a
 * symbol from the board's profile.mk, so this file cannot disagree with it.
 *
 *   __tfw_text_base, __tfw_text_size  the image's code region: the app slot,
 *                                     or tfw-boot's region (RRAM)
 *   __tfw_ram_base                    the SRAM bank .data and .bss start in
 *   __tfw_resetmark                   the reset marker, 4 bytes below
 *   __tfw_handoff                     the 112-byte CDI handoff block, at
 *                                     RAM_END - 111 in both images
 *   __stack                           the initial stack pointer, below both
 *
 * The core's table (src/hal/cortex-m/hal_early.c) holds the sixteen
 * exceptions and IRQs 0-79, 96 words. This SoC's console UARTE is IRQ 198, so
 * src/hal/cortex-m/nrf54l/hal_soc.c supplies IRQs 80-198 in a section of its
 * own, placed straight after: one table of 215 words, 860 bytes. VTOR takes
 * the table's size rounded up to a power of two as its alignment, so the code
 * region must start on a 1 KiB boundary.
 */

ENTRY(tfw_cm_reset)

SECTIONS
{
    /* The vector table first: VTOR and the reset fetch read word 0 (the
     * initial stack pointer) and word 1 (the reset handler) from the region's
     * base. The SoC's tail follows the core's head with no gap. */
    .text __tfw_text_base :
    {
        KEEP(*(.vectors))
        __tfw_vectors_soc = .;
        KEEP(*(.vectors.soc))
        __tfw_vectors_end = .;
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
        . = ALIGN(4);
    }

    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    }

    . = ALIGN(4);
    __data_load_start = .;

    .data __tfw_ram_base : AT(__data_load_start)
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    }

    /* The last byte the image occupies in its code region, read by mk/boot.mk's
     * boot-load-end-check exactly as the AVR's crt symbol of the same name. */
    __data_load_end = __data_load_start + SIZEOF(.data);

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    /* Survives a reset: neither copied nor zeroed. */
    .noinit (NOLOAD) :
    {
        *(.noinit .noinit.*)
        . = ALIGN(4);
    }

    __heap_start = .;
    end = .;

    /* Pinned, as the handoff block is on AVR (mk/handoff.ld): the two images
     * name the same addresses because both links place these sections from
     * the same symbols. */
    .resetmark __tfw_resetmark (NOLOAD) :
    {
        KEEP(*(.resetmark))
    }

    .faultframe __tfw_faultframe (NOLOAD) :
    {
        KEEP(*(.faultframe))
    }

    .handoff __tfw_handoff (NOLOAD) :
    {
        KEEP(*(.handoff))
    }

    ASSERT((__tfw_text_base & 0x3FF) == 0,
           "the code region is not 1 KiB-aligned: VTOR cannot take this SoC's table")
    ASSERT(__tfw_vectors_soc == __tfw_text_base + (16 + 80) * 4,
           "the SoC's vector tail does not follow the core's 96 entries")
    ASSERT(__tfw_vectors_end == __tfw_text_base + (16 + 199) * 4,
           "the vector table is not 16 exceptions and IRQs 0-198")
    ASSERT(__data_load_end <= __tfw_text_base + __tfw_text_size,
           "the image runs past its code region")
    ASSERT(__heap_start <= __stack,
           ".data, .bss and .noinit run into the stack and the handoff block")
}
