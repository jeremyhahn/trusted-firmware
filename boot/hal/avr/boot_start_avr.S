/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's startup, in place of the avr-libc crt (linked -nostartfiles,
 * mk/arch/avr.mk ARCH_BOOT_PLACEMENT_LDFLAGS).
 *
 * WHY NOT THE CRT: its first 228 bytes on the 2560 are a 57-entry interrupt
 * vector table, and the boot takes no interrupt -- interrupts are never
 * enabled here and IVSEL is never set (boot/boot_main.c), so every entry but
 * the reset one is unreachable. Those bytes are the boot section's.
 *
 * WHAT IT KEEPS: everything the crt did that this image needs, in the crt's
 * order. This code is in `.vectors`, which the default linker script puts
 * first in .text, so it sits at the boot section's start where BOOTRST enters.
 * It cannot simply run on into `.init0`: the script places .progmem and
 * .trampolines between the two, so it jumps over them. From there the .initN
 * sections run as the script orders them -- .init3 the shared early init
 * (src/hal/avr/hal_early.c), .init4 libgcc's __do_copy_data and
 * __do_clear_bss, which the compiler pulls in for an image with .data and
 * .bss and which -nostartfiles does not drop -- and .init9 calls main.
 *
 * The first instruction is also `__vectors`, the name the crt gave the image's
 * start: tools read that symbol as where the image sits in flash -- simavr's
 * loader takes its flash base from it (test/integration/simavr/boot_harness.c:1009)
 * and, without it, loads the boot at 0. A label, so no byte changes.
 *
 * The stack starts at __stack, NOT at RAMEND: mk/arch/avr.mk defines it below
 * the CDI handoff block, which the top of SRAM holds.
 */

#include <avr/io.h>

	.section .vectors,"ax",@progbits
	.global	tfw_boot_start
	.global	__vectors
	.type	__vectors, @function
__vectors:
tfw_boot_start:
	clr	r1
	out	_SFR_IO_ADDR(SREG), r1
	ldi	r28, lo8(__stack)
	ldi	r29, hi8(__stack)
	out	_SFR_IO_ADDR(SPH), r29
	out	_SFR_IO_ADDR(SPL), r28
#ifdef __AVR_HAVE_EIJMP_EICALL__
	/* As the crt did: on a three-byte PC an indirect call goes to EIND:Z, so
	 * EIND is this image's 128 KB page until boot/hal/avr/boot_hal_avr.c
	 * zeroes it for the jump to the application. */
	ldi	r16, hh8(pm(tfw_boot_start))
	out	_SFR_IO_ADDR(EIND), r16
#endif
	jmp	tfw_boot_init

	.section .init0,"ax",@progbits
tfw_boot_init:

	.section .init9,"ax",@progbits
	call	main
1:	rjmp	1b
