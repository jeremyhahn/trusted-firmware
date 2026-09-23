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
/* Register addresses.
 *
 * ONE definition, used by the device and by the host alike. The earlier
 * arrangement had an #ifdef __AVR__ arm taking &PINB from <avr/io.h> and an
 * #else arm with the number written out from the datasheet, which meant the
 * table the tests verified was not provably the table that shipped -- the two
 * arms could disagree and nothing would say so.
 *
 * Now the number is the only source, and avr-libc CHECKS it: the asserts at the
 * bottom of this file compare every address against <avr/io.h> whenever this is
 * compiled for AVR. A transposed digit is a compile error on the firmware
 * build, naming the register.
 */
#define AG_ADDR_TCCR1A 0x80u
#define AG_ADDR_OCR1AL 0x88u
#define AG_ADDR_OCR1BL 0x8Au
#define AG_ADDR_TCCR2A 0xB0u
#define AG_ADDR_OCR2A  0xB3u
#define AG_ADDR_OCR2B  0xB4u

/* COM bit positions within TCCRnA. Constants rather than <avr/io.h> symbols so
 * the table compiles on the host, where that header does not exist -- and the
 * values are the same ones the datasheet gives, so what the tests check is what
 * the device writes. */
#define COM2B1_BIT 5u
#define COM1A1_BIT 7u
#define COM1B1_BIT 5u
#define COM2A1_BIT 7u

#define AG_ADDR_PINB 0x23u  /* DDRB 0x24, PORTB 0x25 */
#define AG_ADDR_PINC 0x26u  /* DDRC 0x27, PORTC 0x28 */
#define AG_ADDR_PIND 0x29u  /* DDRD 0x2A, PORTD 0x2B */

/* The check. avr-libc is the authority on where these live; this asserts the
 * numbers above against it, so the host tests and the shipped image cannot
 * disagree about a single register. Costs nothing at run time and is the only
 * preprocessor conditional a board profile needs. */
#ifdef __AVR__
#include <avr/io.h>
_Static_assert(AG_ADDR_PINB   == _SFR_ADDR(PINB),   "PINB address");
_Static_assert(AG_ADDR_PINC   == _SFR_ADDR(PINC),   "PINC address");
_Static_assert(AG_ADDR_PIND   == _SFR_ADDR(PIND),   "PIND address");
_Static_assert(AG_ADDR_TCCR1A == _SFR_ADDR(TCCR1A), "TCCR1A address");
_Static_assert(AG_ADDR_OCR1AL == _SFR_ADDR(OCR1AL), "OCR1AL address");
_Static_assert(AG_ADDR_OCR1BL == _SFR_ADDR(OCR1BL), "OCR1BL address");
_Static_assert(AG_ADDR_TCCR2A == _SFR_ADDR(TCCR2A), "TCCR2A address");
_Static_assert(AG_ADDR_OCR2A  == _SFR_ADDR(OCR2A),  "OCR2A address");
_Static_assert(AG_ADDR_OCR2B  == _SFR_ADDR(OCR2B),  "OCR2B address");
_Static_assert(COM2B1_BIT == COM2B1, "COM2B1 bit position");
_Static_assert(COM1A1_BIT == COM1A1, "COM1A1 bit position");
_Static_assert(COM1B1_BIT == COM1B1, "COM1B1 bit position");
_Static_assert(COM2A1_BIT == COM2A1, "COM2A1 bit position");
#endif

/* Which USART the firmware talks over.
 *
 * The 328P has one USART and avr-libc spells its vectors without an index.
 *
 * The register names are pasted from AG_UART_N in src/hal/avr/hal_uart.c; the
 * vector names are spelled out because avr-libc is not consistent about them
 * and a pasted name that does not exist compiles to an ordinary function
 * nothing calls -- a device that simply never answers.
 */
#define AG_UART_N          0
#define AG_UART_RX_VECT    USART_RX_vect
#define AG_UART_UDRE_VECT  USART_UDRE_vect

/* The one pin tone can use: the output-compare pin of AG_TONE_TIMER.
 * OC2A is PB3, which the Arduino world calls pin 11. */
#define AG_TONE_PIN   11u

/* The SPI pins' bit positions within port B. The HAL must set their direction
 * before the peripheral works, and it cannot ask the pin table for them:
 * the SPI lines are the hardware's, not logical pins a host names.
 * PB3 MOSI, PB5 SCK, PB2 SS -- pins 11, 13 and 10. */
#define AG_SPI_BIT_MOSI 3u
#define AG_SPI_BIT_SCK  5u
#define AG_SPI_BIT_SS   2u

#endif /* AG_BOARD_ATMEGA328P_H */
