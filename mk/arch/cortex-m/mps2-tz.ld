/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The mps2-tz SoC's images, application and tfw-boot alike (#161).
 *
 * No MEMORY block and no address: mk/arch/cortex-m.mk passes every one as a
 * symbol from the board's profile.mk, so this file cannot disagree with it.
 *
 *   __tfw_text_base, __tfw_text_size  the image's code region: the app slot,
 *                                     or tfw-boot's region
 *   __tfw_ram_base                    the SRAM bank .data and .bss start in
 *   __tfw_resetmark                   the reset marker, 4 bytes below
 *   __tfw_handoff                     the 112-byte CDI handoff block, at
 *                                     RAM_END - 111 in both images
 *   __stack                           the initial stack pointer, below both
 *
 * The code region is RAM on this SoC (QEMU's SSRAM banks), but the image is
 * laid out as for flash: .data's initial values follow the code and the
 * reset handler copies them, so a board whose code region is flash takes the
 * same script.
 */

ENTRY(tfw_cm_reset)

SECTIONS
{
    /* The vector table first: VTOR and the reset fetch read word 0 (the
     * initial stack pointer) and word 1 (the reset handler) from the region's
     * base. */
    .text __tfw_text_base :
    {
        KEEP(*(.vectors))
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

    .handoff __tfw_handoff (NOLOAD) :
    {
        KEEP(*(.handoff))
    }

    ASSERT(__data_load_end <= __tfw_text_base + __tfw_text_size,
           "the image runs past its code region")
    ASSERT(__heap_start <= __stack,
           ".data, .bss and .noinit run into the stack and the handoff block")
}
