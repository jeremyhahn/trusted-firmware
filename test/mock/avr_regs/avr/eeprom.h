/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <avr/eeprom.h> for the host, as wide as src/hal/avr/hal_eeprom.c needs
 * (#163). Declarations only: the suite defines each over its own model of the
 * cells, so it sees every address and every program cycle. The write and the
 * update forms are both declared, so a unit that programs a cell it need not
 * still builds and is caught by the suite. Only on the -I of the suite that
 * tests that unit; the AVR build uses avr-libc's.
 */
#ifndef TFW_TEST_AVR_EEPROM_H
#define TFW_TEST_AVR_EEPROM_H

#include <stddef.h>
#include <stdint.h>

uint8_t eeprom_read_byte(const uint8_t *p);
void    eeprom_read_block(void *dst, const void *src, size_t n);
void    eeprom_write_byte(uint8_t *p, uint8_t value);
void    eeprom_update_byte(uint8_t *p, uint8_t value);
void    eeprom_write_block(const void *src, void *dst, size_t n);
void    eeprom_update_block(const void *src, void *dst, size_t n);

#endif
