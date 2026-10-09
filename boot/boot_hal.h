/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE BOOTLOADER'S HAL CONTRACT -- and why it is not include/tfw/hal.h.
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
 * Substituted at LINK time, exactly as include/tfw/hal.h is, and for the same
 * reasons: boot/hal/avr for the device, test/mock/boot_hal_mock.c for the host.
 * Add a function here, forget the mock, and the host tests fail to LINK rather
 * than failing somewhere nobody has written a test for yet.
 */
#ifndef TFW_BOOT_HAL_H
#define TFW_BOOT_HAL_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/types.h"

/* ---------------------------------------------------------------- flash */

/* Reads program memory. The address is 32-bit because the Mega has 256 KB and
 * a 16-bit program address silently aliases above 64 KB -- which would make the
 * measurement of a large image the measurement of a different image, and it
 * would still verify against itself. */
void tfw_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len);

/* Erases and rewrites one page. `addr` must be page-aligned and `data` must
 * carry exactly tfw_boot_flash_page_size() bytes.
 *
 * Erase and write are one operation here rather than two, because every caller
 * wants both and a page erased by one call and written by another is a window
 * in which a reset leaves 0xFF where code should be. */
tfw_status_t tfw_boot_flash_write_page(uint32_t addr, const uint8_t *data);

uint16_t tfw_boot_flash_page_size(void);

/* ------------------------------------------------------------------ nvm */

/* The non-volatile store the platform block (TFW_BOOT_EE_* in
 * include/tfw/boot.h) lives in, addressed from its own zero. On AVR it is the
 * part's EEPROM; a part without one maps it wherever its HAL keeps state that
 * survives a reset, and nothing above this line needs to know which. A span
 * past the end of the store is TFW_ERR_RANGE, never a partial transfer. */
tfw_status_t tfw_boot_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len);
tfw_status_t tfw_boot_nvm_write(uint16_t addr, const uint8_t *buf, uint8_t len);

/* ----------------------------------------------------------------- uart */

void    tfw_boot_uart_init(uint32_t baud);

/* 1 if a byte was taken, 0 if none was waiting. Never blocks. */
uint8_t tfw_boot_uart_rx(uint8_t *out);

/* Blocks until the byte is in the shift register. The bootloader has nothing
 * to overlap with, so backpressure would be state kept for no benefit. */
void    tfw_boot_uart_tx(uint8_t byte);

/* --------------------------------------------------------------- handoff */

/* Places the CDI handoff block where the application will find it. Separate
 * from an ordinary memory write because the address is a linker fact and
 * belongs on the far side of this contract; see TFW_BOOT_HANDOFF_MAGIC in
 * include/tfw/boot.h for what the application does with it. */
void tfw_boot_handoff_write(const uint8_t *block, uint8_t len);

/* Reads the first `len` bytes back from the same place. The application leaves
 * its update-session request there (TFW_BOOT_REQUEST_MAGIC in
 * include/tfw/boot.h) and tfw-boot reads it before it writes a handoff of its own
 * over it. */
void tfw_boot_handoff_read(uint8_t *buf, uint8_t len);

/* ----------------------------------------------------------------- tick */

/* TFW_BOOT_TICK_HZ: how many ticks the bootloader counts a second, from which
 * boot/boot_main.c sets the update session's time limits (include/tfw/config.h).
 * A constant, not a function, stated by the architecture's boot_tick.h in
 * $(BOOT_HAL_DIR) -- boot/hal/avr/boot_tick.h says what a tick is there. */

/* ----------------------------------------------------------- reset cause */

/* Why the part last reset, as THIS image's own early init observed it -- before
 * anything could clear it. The bootloader runs first, so this is the only
 * record there is. Already abstract: the register it is read from, and how its
 * bits are laid out, are the HAL's business and nothing above this contract
 * decodes them.
 *
 * The early init that latches it is the architecture's and is SHARED with the
 * application image (src/hal/<arch>/hal_early.c), so that "clear the flags and
 * disarm the watchdog" exists once. The latch itself is declared by that file's
 * own header, which boot/hal/<arch> includes; the portable bootloader sees only
 * the decoded cause here and never names the architecture's latch. */
tfw_reset_cause_t tfw_boot_reset_cause(void);

/* ---------------------------------------------------------------- control */

/* Hands control to the application at address zero. Does not return.
 *
 * Every peripheral the bootloader touched is returned to its reset state first.
 * An application that inherited a configured USART would appear to work until
 * something reset the part without a bootloader run in between. */
void tfw_boot_jump_application(void);

/* Does not return. A real hardware reset, not a jump to zero. */
void tfw_boot_reset(void);

#endif /* TFW_BOOT_HAL_H */
