/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE BOOTLOADER'S HAL CONTRACT -- and why it is not include/ag/hal.h.
 *
 * The application's HAL is a good HAL for an application. Linking it into the
 * boot section would cost three things a 4 KB boot section cannot pay:
 *
 *   1. Its UART is interrupt-driven, with two ring buffers and two ISRs. An
 *      ISR in the boot section needs the interrupt vector table moved there
 *      (IVSEL), which must then be moved back before the application runs, and
 *      a reset in between leaves the part dispatching application interrupts
 *      into bootloader code. The bootloader polls instead. It has nothing else
 *      to do while it waits, so the entire justification for the ring buffers
 *      is absent here.
 *
 *   2. It has no flash access at all, because the application has no business
 *      having any: --gc-sections cannot drop a capability the application was
 *      given, and a generic "read any flash address" primitive in the
 *      application would hand away everything the lock bits protect. See
 *      docs/security/fuses-and-lockbits.md, which says exactly this.
 *
 *   3. Its init brings up the ADC, the timers and the buses. The bootloader
 *      leaves every peripheral in its reset state, so the application finds the
 *      part as it would after a bare reset and needs no knowledge that a
 *      bootloader ran.
 *
 * Substituted at LINK time, exactly as include/ag/hal.h is, and for the same
 * reasons: boot/hal/avr for the device, test/mock/boot_hal_mock.c for the host.
 * Add a function here, forget the mock, and the host tests fail to LINK rather
 * than failing somewhere nobody has written a test for yet.
 */
#ifndef AG_BOOT_HAL_H
#define AG_BOOT_HAL_H

#include <stdint.h>
#include "ag/status.h"

/* ---------------------------------------------------------------- flash */

/* Reads program memory. The address is 32-bit because the Mega has 256 KB and
 * a 16-bit program address silently aliases above 64 KB -- which would make the
 * measurement of a large image the measurement of a different image, and it
 * would still verify against itself. */
void ag_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len);

/* Erases and rewrites one page. `addr` must be page-aligned and `data` must
 * carry exactly ag_boot_flash_page_size() bytes.
 *
 * Erase and write are one operation here rather than two, because every caller
 * wants both and a page erased by one call and written by another is a window
 * in which a reset leaves 0xFF where code should be. */
ag_status_t ag_boot_flash_write_page(uint32_t addr, const uint8_t *data);

uint16_t ag_boot_flash_page_size(void);

/* --------------------------------------------------------------- eeprom */

ag_status_t ag_boot_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len);
ag_status_t ag_boot_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len);

/* ----------------------------------------------------------------- uart */

void    ag_boot_uart_init(uint32_t baud);

/* 1 if a byte was taken, 0 if none was waiting. Never blocks. */
uint8_t ag_boot_uart_rx(uint8_t *out);

/* Blocks until the byte is in the shift register. The bootloader has nothing
 * to overlap with, so backpressure would be state kept for no benefit. */
void    ag_boot_uart_tx(uint8_t byte);

/* --------------------------------------------------------------- handoff */

/* Places the CDI handoff block where the application will find it. Separate
 * from an ordinary memory write because the address is a linker fact and
 * belongs on the far side of this contract; see AG_BOOT_HANDOFF_MAGIC in
 * include/ag/boot.h for what the application does with it. */
void ag_boot_handoff_write(const uint8_t *block, uint8_t len);

/* ---------------------------------------------------------------- control */

/* Hands control to the application at address zero. Does not return.
 *
 * Every peripheral the bootloader touched is returned to its reset state first.
 * An application that inherited a configured USART would appear to work until
 * something reset the part without a bootloader run in between. */
void ag_boot_jump_application(void);

/* Does not return. A real hardware reset, not a jump to zero. */
void ag_boot_reset(void);

#endif /* AG_BOOT_HAL_H */
