/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Every Cortex-M image, application and tfw-boot alike, on every SoC (#161;
 * mps2 and mcxn94x, SPEC row mcxn947). On mps2 that is the AN505 (Cortex-M33,
 * #161) and the AN500 (Cortex-M7): nothing here is TrustZone's, the script
 * places sections at the addresses it is given, so the two images' different
 * memory maps are their profiles' alone.
 *
 * No MEMORY block and no address: mk/arch/cortex-m.mk passes every one as a
 * symbol from the board's profile.mk, so this file cannot disagree with it.
 *
 *   __tfw_text_base, __tfw_text_size  the image's code region: the app slot,
 *                                     or tfw-boot's region
 *   __tfw_ram_base                    the SRAM bank .data and .bss start in
 *   __tfw_faultframe                  the 12-byte fault record, below
 *   __tfw_resetmark                   the reset marker, 4 bytes below
 *   __tfw_handoff                     the 112-byte CDI handoff block, at
 *                                     RAM_END - 111 in both images
 *   __stack                           the initial stack pointer, below both
 *
 * The image is laid out as for flash: .data's initial values follow the code
 * and the reset handler copies them. That is what the MCXN94x's flash needs,
 * and it is as true of mps2, whose code region is RAM (QEMU's SSRAM banks),
 * so the two SoCs take the same script.
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
        /* Sections only a SoC's own script places (rp2350.ld, nrf54l.ld). In
         * this one --gc-sections would drop them or they would land in flash,
         * so they are collected here for the ASSERT below to fail the link. */
        __tfw_soc_only = .;
        KEEP(*(.image_def .vectors.soc))
        *(.ramfunc .ramfunc.*)
        __tfw_soc_only_end = .;
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
        . = ALIGN(4);
    }

    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    }

    /* A remoteproc resource table, for a SoC whose images Linux loads
     * (imx95.ld says why it is here). Nothing on the other SoCs defines the
     * input section, so ld drops the empty output section from their images. */
    .resource_table : ALIGN(4)
    {
        KEEP(*(.resource_table))
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

    ASSERT(__tfw_soc_only_end == __tfw_soc_only,
           "an .image_def, .vectors.soc or .ramfunc section linked with image.ld: the board's profile.mk must name its SoC's script")
    ASSERT(__data_load_end <= __tfw_text_base + __tfw_text_size,
           "the image runs past its code region")
    ASSERT(__heap_start <= __stack,
           ".data, .bss and .noinit run into the stack and the handoff block")
}
