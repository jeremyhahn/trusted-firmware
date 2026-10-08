/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bootloader's AVR HAL.
 *
 * Under boot/hal/avr, which is a register-poking layer and is therefore, like
 * src/hal/avr, one of the places a target conditional is permitted. There is
 * one in this file and it is the far-flash read on parts with more than 64 KB;
 * everything else is plain code.
 */

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "tfw/boot.h"
#include "../../boot_hal.h"
#include "avr_usart_regs.h"

/* ---------------------------------------------------------------- flash */

void tfw_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    for (i = 0u; i < len; i++) {
#if FLASHEND > 0xFFFFUL
        /* Above 64 KB a 16-bit program address silently aliases, so the Mega
         * needs the far form. Getting this wrong would make the measurement of
         * a large image the measurement of a DIFFERENT image -- and it would
         * still verify against itself, because the signing tool would be
         * hashing the file while the device hashed the wrong half of flash. */
        buf[i] = pgm_read_byte_far(addr + i);
#else
        buf[i] = pgm_read_byte((uint16_t)(addr + i));
#endif
    }
}

uint16_t tfw_boot_flash_page_size(void)
{
    return (uint16_t)SPM_PAGESIZE;
}

tfw_status_t tfw_boot_flash_write_page(uint32_t addr, const uint8_t *data)
{
    uint16_t i;
    uint8_t  sreg;

    if ((addr % (uint32_t)SPM_PAGESIZE) != 0u) {
        return TFW_ERR_RANGE;
    }
    /*
     * THE BOOT SECTION IS NOT WRITABLE FROM HERE, and this check is the reason
     * rather than BLB0 alone.
     *
     * BLB0 mode 3 stops the APPLICATION writing the boot section; it does not
     * stop the boot section writing itself, and SPM from the boot section is
     * exactly what is executing. So an OTA image one page too long would
     * overwrite the bootloader with application code, and the device would
     * never run anything again. One comparison prevents it.
     */
    if (addr + (uint32_t)SPM_PAGESIZE > TFW_BOOT_APP_END) {
        return TFW_ERR_PROTECTED_REGION;
    }

    /* Interrupts are never enabled in this image, so this saves and restores a
     * flag that is already clear. It stays because boot_page_erase's timed
     * sequence is only correct with interrupts off, and a future change that
     * enables them must not silently turn this into a corrupting write. */
    sreg = SREG;
    cli();

    boot_page_erase(addr);
    boot_spm_busy_wait();

    /* Filled a WORD at a time, little-endian, because SPM writes the page
     * buffer in words and there is no byte form. */
    for (i = 0u; i < (uint16_t)SPM_PAGESIZE; i += 2u) {
        uint16_t w = (uint16_t)((uint16_t)data[i] | ((uint16_t)data[i + 1u] << 8));
        boot_page_fill(addr + i, w);
    }

    boot_page_write(addr);
    boot_spm_busy_wait();

    /* Without this the application section reads as the page buffer rather than
     * as flash, so the very next measurement would hash something that is not
     * what was written. */
    boot_rww_enable();

    SREG = sreg;
    return TFW_OK;
}

/* --------------------------------------------------------------- eeprom */

tfw_status_t tfw_boot_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    eeprom_read_block(buf, (const void *)(uintptr_t)addr, len);
    return TFW_OK;
}

tfw_status_t tfw_boot_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > (uint32_t)TFW_EEPROM_SIZE) {
        return TFW_ERR_RANGE;
    }
    /* `update` rather than `write`: a byte already holding the wanted value is
     * skipped. EEPROM has a finite endurance and the bootloader rewrites this
     * region on every boot that advances anything, so writing all 128 bytes
     * each time would spend the part's endurance on bytes that did not change.
     */
    eeprom_update_block(buf, (void *)(uintptr_t)addr, len);
    return TFW_OK;
}

/* ----------------------------------------------------------------- uart */

void tfw_boot_uart_init(uint32_t baud)
{
    /* Double-speed, for the same reason the application's UART uses it: at
     * 16 MHz and 115200 the normal divisor has 3.5% error, which is outside
     * what a UART tolerates over a full frame. */
    uint16_t ubrr = (uint16_t)((F_CPU / (8UL * baud)) - 1UL);

    TFW_UBRRH = (uint8_t)(ubrr >> 8);
    TFW_UBRRL = (uint8_t)(ubrr & 0xFFu);
    TFW_UCSRA = (uint8_t)_BV(TFW_U2X);
    TFW_UCSRC = (uint8_t)(_BV(TFW_UCSZ1) | _BV(TFW_UCSZ0));   /* 8N1 */
    TFW_UCSRB = (uint8_t)(_BV(TFW_RXEN) | _BV(TFW_TXEN));     /* no RXCIE: polled */
}

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    if ((TFW_UCSRA & (uint8_t)_BV(TFW_RXC)) == 0u) {
        return 0u;
    }
    *out = TFW_UDR;
    return 1u;
}

void tfw_boot_uart_tx(uint8_t byte)
{
    while ((TFW_UCSRA & (uint8_t)_BV(TFW_UDRE)) == 0u) { }
    TFW_UDR = byte;
}

/* -------------------------------------------------------------- handoff */

/*
 * The handoff block sits at the very top of SRAM, in its own `.handoff`
 * section -- NOT the generic `.noinit` this same image's hal_early.c also
 * uses for tfw_hal_early_flags.
 *
 * Uninitialised-but-not-zeroed storage because the application's C runtime
 * zeroes .bss before main() and would erase the CDI before anything could
 * read it -- the same reasoning `.noinit` exists for. A section OF ITS OWN
 * rather than that one, because mk/boot.mk pins this section's address with
 * `--section-start`: pinning the generic `.noinit` output section would carry
 * every other object assigned to it to the same fixed spot, and
 * tfw_hal_early_flags is exactly such an object in this same image.
 *
 * At the top of SRAM because that is an address both images can name without
 * a shared linker script: the bootloader's own stack is moved below it by the
 * -Wl,--defsym in mk/boot.mk, and the application places its own reader
 * (src/hal/avr/hal_handoff.c) at the identical address via the same formula
 * in mk/avr.mk -- see include/tfw/boot.h and src/core/handoff.c for the
 * application's side of this handoff.
 *
 * `.handoff` IS NOLOAD, genuinely -- mk/handoff.ld (linked into BOOT_LDFLAGS)
 * splices `.handoff (NOLOAD) : { *(.handoff) }` into the default script, the
 * same SHT_NOBITS treatment `.bss` already gets. That is what keeps this
 * array's 80 bytes out of the FLASH hex this same Makefile produces, rather
 * than an `-R .handoff` at every point an image gets derived from the ELF.
 */
static uint8_t handoff[TFW_BOOT_HANDOFF_LEN] __attribute__((section(".handoff")));

void tfw_boot_handoff_write(const uint8_t *block, uint8_t len)
{
    memcpy(handoff, block, len);
}

/* -------------------------------------------------------------- control */

void tfw_boot_handoff_read(uint8_t *buf, uint8_t len)
{
    memcpy(buf, handoff, len);
}

void tfw_boot_jump_application(void)
{
    /* Every peripheral the bootloader touched, back to its reset value. An
     * application that inherited a configured USART would work until the first
     * reset that did not run a bootloader, which is the kind of bug that is
     * found in the field and not on a bench. */
    TFW_UCSRB = 0u;
    TFW_UCSRA = 0u;
    TFW_UBRRH = 0u;
    TFW_UBRRL = 0u;

    /* The RWW section must be readable before anything executes from it. */
    boot_rww_enable();

#ifdef EIND
    /*
     * EIND FIRST, ON EVERY PART WITH MORE THAN 128 KB OF FLASH, AND THE
     * SYMPTOM OF FORGETTING IT IS NOT A CRASH.
     *
     * Above 128 KB the program counter is three bytes, so an indirect call is
     * `eicall` and its target is EIND:Z -- EIND picking the 128 KB page. The
     * crt this image links sets EIND to the page `__vectors` is in, which for a
     * bootloader at 0x3E000 is page 1. `((void(*)(void))0)()` therefore jumps
     * to 0x20000 and not to 0, which on a Mega is erased flash: 0xFF decodes as
     * a long run of SBRS, the part walks the whole upper half of its flash, and
     * falls straight back into the bootloader -- which measures, verifies and
     * jumps again, forever, with nothing on the wire to say why.
     *
     * The conditional is on EIND rather than on FLASHEND because EIND is
     * exactly the thing being written; a part without the register is a part
     * with a two-byte PC, where `icall` needs nothing. This file is under
     * boot/hal/avr, one of the four places a target conditional is permitted.
     */
    EIND = 0u;
#endif

    /* A plain call through a null function pointer: the application's reset
     * vector is at address zero. Not a watchdog reset, because a reset would
     * come straight back into the bootloader -- BOOTRST is programmed -- and
     * the part would never reach the application at all. */
    ((void (*)(void))0)();

    for (;;) { }   /* unreachable */
}

void tfw_boot_reset(void)
{
    /* The watchdog, not a jump to zero, for the reason src/hal/avr/hal_sys.c
     * gives: a jump leaves every peripheral configured and half the chip
     * believing it is still running. With BOOTRST programmed this lands back
     * here, which is what a host asking the bootloader to reset wants. */
    cli();
    WDTCSR = (uint8_t)(_BV(WDCE) | _BV(WDE));
    WDTCSR = (uint8_t)_BV(WDE);
    for (;;) { }
}
