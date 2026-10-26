/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 SoC's images, application and tfw-boot alike (row rp2350).
 *
 * mps2-tz.ld's layout and its symbols (mk/arch/cortex-m.mk passes every
 * address from the board's profile.mk), with one addition: the boot ROM's
 * IMAGE_DEF block right after the vector table. The ROM boots an image only
 * if it finds a valid block within the first 4 KiB of flash ([RP-DS], boot
 * ROM chapter, "Blocks"; release 8 §5.1.12 and §5.9.1), and it takes the initial stack pointer and the
 * entry point from the vector table at the image's start. tfw-boot is the
 * image at the flash's base; the application, in its slot after it, carries
 * the block too, so either image is bootable from that base.
 *
 * The code region is the XIP window over the QSPI flash, so .data's initial
 * values follow the code and the reset handler copies them. XIP at
 * 0x10000000 and SRAM0-7 at 0x20000000-0x2007FFFF: [RP-DS] release 8 §2.2,
 * tables 10-11 (the board's profile.mk passes both).
 */

ENTRY(tfw_cm_reset)

SECTIONS
{
    /* The vector table first: the ROM and VTOR read word 0 (the initial stack
     * pointer) and word 1 (the reset handler) from the region's base. */
    .text __tfw_text_base :
    {
        KEEP(*(.vectors))
        __tfw_image_def = .;
        KEEP(*(.image_def))
        __tfw_image_def_end = .;
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
        /* Code that runs while XIP is off (boot/hal/cortex-m/rp2350/
         * rp_flash_rom.c): the reset handler copies it to SRAM with .data. */
        . = ALIGN(4);
        *(.ramfunc .ramfunc.*)
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

    ASSERT(__tfw_image_def_end - __tfw_image_def == 20,
           "the IMAGE_DEF block is missing or not the 5 words soc_calc.h encodes")
    ASSERT(__tfw_image_def_end <= __tfw_text_base + 0x1000,
           "the boot ROM looks for the IMAGE_DEF block in the first 4 KiB only")
    ASSERT(__data_load_end <= __tfw_text_base + __tfw_text_size,
           "the image runs past its code region")
    ASSERT(__heap_start <= __stack,
           ".data, .bss and .noinit run into the stack and the handoff block")
}
