/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ATmega328P -- Arduino Uno, Nano and Pro Mini. The primary target.
 *
 * 32 KB flash, 2 KB SRAM, 1 KB EEPROM.
 *
 * A board profile states only its deviations from include/ag/config.h. This one
 * is a full-featured part, so it deviates mostly in sizing.
 */
#ifndef AG_BOARD_ATMEGA328P_H
#define AG_BOARD_ATMEGA328P_H

#define AG_BOARD_NAME "atmega328p"
#define AG_BOARD_ID   0x01u

/* D0..D13 are 0..13; A0..A5 are 14..19; A6 and A7 are 20 and 21 and exist only
 * on the TQFP package the Nano uses. These are the numbers printed on the
 * board, because that is the number the person holding it can see. */
#define AG_PIN_COUNT  22u
#define AG_PORT_COUNT 3u    /* B=0, C=1, D=2 */
#define AG_PWM_COUNT  4u
#define AG_ADC_COUNT  8u
#define AG_EEPROM_SIZE 1024u

/* Timer0 drives the millisecond tick, which costs pins 5 and 6 their PWM.
 * The descriptor marks them AG_PWM_RSVD so CAPS tells a host up front rather
 * than letting it write a duty cycle into a timer that is counting something
 * else. */
#define AG_TICK_TIMER 0u
#define AG_TONE_TIMER 2u

#define AG_MAX_PAYLOAD 64u
#define AG_SCHED_SLOTS 8u

/* Attestation is on. This part has the flash for it, and a device that cannot
 * say what it is running is not the device this project is for.
 *
 * The tier it can honestly claim depends on what is on the board: with no
 * secure element the provider reports AG_CRYPTO_SOFTWARE and refuses to sign at
 * all until a UDS has been provisioned, which is the correct behaviour for a
 * device that genuinely has no identity yet. */
#define AG_FEATURE_ATTEST 1

/* Half the part, each. The budget is a gate, not an estimate: `make size-check`
 * fails the build when an image exceeds it, so flash creep is caught on the
 * commit that causes it rather than the day someone tries to add a feature and
 * finds there is no room. */
#define AG_FLASH_BUDGET 16384u
#define AG_RAM_BUDGET   1024u

/*
 * Port register addresses.
 *
 * On the device these come from <avr/io.h>. On the host they are the same
 * numbers, written out from the datasheet -- so the table the tests verify is
 * byte for byte the table that ships, rather than a stand-in that could agree
 * with a broken one.
 *
 * Every classic AVR lays out PINx, DDRx, PORTx consecutively, so one address
 * locates all three.
 */
/* Output-compare registers. Same reasoning as the ports: real addresses on the
 * device, the same numbers written out from the datasheet on the host, so the
 * table under test is the table that ships. */
#ifdef __AVR__
#include <avr/io.h>
#define AG_ADDR_TCCR1A ((uint16_t)(uintptr_t)&TCCR1A)
#define AG_ADDR_OCR1AL ((uint16_t)(uintptr_t)&OCR1AL)
#define AG_ADDR_OCR1BL ((uint16_t)(uintptr_t)&OCR1BL)
#define AG_ADDR_TCCR2A ((uint16_t)(uintptr_t)&TCCR2A)
#define AG_ADDR_OCR2A  ((uint16_t)(uintptr_t)&OCR2A)
#define AG_ADDR_OCR2B  ((uint16_t)(uintptr_t)&OCR2B)
#else
#define AG_ADDR_TCCR1A 0x80u
#define AG_ADDR_OCR1AL 0x88u
#define AG_ADDR_OCR1BL 0x8Au
#define AG_ADDR_TCCR2A 0xB0u
#define AG_ADDR_OCR2A  0xB3u
#define AG_ADDR_OCR2B  0xB4u
#endif

/* COM bit positions within TCCRnA. Constants rather than <avr/io.h> symbols so
 * the table compiles on the host, where that header does not exist -- and the
 * values are the same ones the datasheet gives, so what the tests check is what
 * the device writes. */
#define COM2B1_BIT 5u
#define COM1A1_BIT 7u
#define COM1B1_BIT 5u
#define COM2A1_BIT 7u

#ifdef __AVR__
#define AG_ADDR_PINB ((uint16_t)(uintptr_t)&PINB)
#define AG_ADDR_PINC ((uint16_t)(uintptr_t)&PINC)
#define AG_ADDR_PIND ((uint16_t)(uintptr_t)&PIND)
#else
#define AG_ADDR_PINB 0x23u  /* DDRB 0x24, PORTB 0x25 */
#define AG_ADDR_PINC 0x26u  /* DDRC 0x27, PORTC 0x28 */
#define AG_ADDR_PIND 0x29u  /* DDRD 0x2A, PORTD 0x2B */
#endif

#endif /* AG_BOARD_ATMEGA328P_H */
