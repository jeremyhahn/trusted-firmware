/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ATmega32U4 -- Arduino Leonardo, Micro and Pro Micro.
 *
 * 32 KB flash, 2560 bytes SRAM, 1 KB EEPROM.
 *
 * The part is bigger than the 328P in every dimension except one that matters:
 * on a Leonardo or a Micro the top 4 KB of flash holds the Caterina USB
 * bootloader, so only 28 KB is ever available to an application. The budget
 * below is set against 28 KB, not 32 KB, because a build that fits the part and
 * not the board is a build that cannot be installed.
 *
 * A board profile states only its deviations from include/ag/config.h.
 */
#ifndef AG_BOARD_ATMEGA32U4_H
#define AG_BOARD_ATMEGA32U4_H

#define AG_BOARD_NAME "atmega32u4"
#define AG_BOARD_ID   0x03u   /* 0x01 is the 328P, 0x02 the 2560 */

/*
 * Pin numbering: the Leonardo/Micro core's map, 0..23, with no holes.
 *
 * D0..D13 are the numbers printed on the board. 14..17 are MISO, SCK, MOSI and
 * SS -- brought out on the Micro and the Pro Micro, and present on the
 * Leonardo's ICSP header even though its silkscreen does not number them. They
 * are numbered here because the Arduino core numbers them, and because leaving
 * a hole would put A0 at 18 with pins 14..17 refusing every request, which a
 * host cannot distinguish from a firmware bug.
 *
 * A0..A5 are therefore 18..23, exactly as the core has them.
 *
 * What a host sees: a contiguous 0..23 where every pin exists and every pin is
 * addressable. That is the whole descriptor -- CAPS reports the flags, and a
 * host never needs a per-board table of which numbers are real.
 *
 * The core's extra analog aliases A6..A11 are deliberately NOT given numbers of
 * their own. They are not extra pins; they are ADC channels wired to pins that
 * already have a number (A6 is D4, A7 is D6, A8 is D8, A9 is D9, A10 is D10 and
 * A11 is D12). Numbering them would put two descriptor rows on one port bit,
 * which is precisely the collision the descriptor tests exist to catch. Instead
 * those six pins carry their ADC channel on their digital number, so a host
 * reads ADC8 by naming pin 4.
 */
#define AG_PIN_COUNT  24u
#define AG_PORT_COUNT 5u    /* B=0, C=1, D=2, E=3, F=4 */
#define AG_PWM_COUNT  3u

/*
 * ADC channels, and the one place this part does not behave like a 328P.
 *
 * The 32U4's channel space is NOT a contiguous 0..n. ADC0, ADC1 and ADC4..ADC13
 * exist; ADC2 and ADC3 are not bonded out on any package, so the numbering has
 * a hole in the middle. Channels above 7 sit in a second bank selected by MUX5
 * in ADCSRB rather than by a MUX bit in ADMUX.
 *
 * src/hal/avr/hal_adc.c already expresses exactly that: bank = channel >> 3 and
 * MUX2:0 = channel & 7. So the datasheet's channel numbers go into the table
 * unaltered -- ADC7 is 7 and ADC13 is 13 -- and the HAL selects bank 1 and
 * MUX2:0 = 5 for channel 13 without the board knowing MUX5 exists. The holes at
 * 2 and 3 need no expression at all: no pin claims them, so no request can
 * reach them.
 *
 * AG_ADC_COUNT is the bound the HAL range-checks against, so it must be one
 * past the highest REAL channel (13), not a count of the twelve that exist.
 */
#define AG_ADC_COUNT  14u
#define AG_EEPROM_SIZE 1024u

/* Timer0 drives the millisecond tick, which costs OC0A and OC0B their PWM.
 * On this part those are PB7 (pin 11) and PD0 (pin 3). The descriptor marks
 * both AG_PWM_RSVD so CAPS tells a host up front rather than letting it write
 * a duty cycle into a timer that is counting something else.
 *
 * AG_TONE_TIMER is 3 rather than the 328P's 2 for the blunt reason that this
 * part has no Timer2 at all: its timer complement is 0, 1, 3 and the
 * high-speed 4. It names the timer tone WOULD use, not one it does -- this
 * build sets AG_FEATURE_TONE 0 for the reason given beside that define, and
 * the tone HAL asserts AG_TONE_TIMER == 2, so flipping the feature on without
 * writing a 16-bit tone path is a build error rather than a wrong pitch. */
#define AG_TICK_TIMER 0u
#define AG_TONE_TIMER 3u

#define AG_MAX_PAYLOAD 64u

/* Twelve slots rather than the 328P's eight. A slot is six bytes, so this
 * spends 72 bytes against 48 -- an extra 24 out of the 512 bytes of SRAM this
 * part has over a 328P. The pin count rose by two and the part has room, so
 * the limit that a host hits first should not be the one that is cheapest to
 * raise. */
#define AG_SCHED_SLOTS 12u

/* Attestation is on, for the same reason as on the 328P: a device that cannot
 * say what it is running is not the device this project is for. The reduced
 * application space is the reason the budget is tight, not a reason to drop
 * the feature. */
#define AG_FEATURE_ATTEST 1

/*
 * The alias certificate is OFF on this part, and only on this part.
 *
 * It does not fit. The certificate builder, its DER template and the claims-set
 * encoder are 2 540 bytes; an element-backed image here measures 14 706 against
 * a 14 336 budget, which is 370 over. The budget is half the usable application
 * space, and the usable space is 28 672 rather than 32 768 because 4 096 bytes
 * of this part are a DFU bootloader that cannot be reclaimed -- so there is no
 * number to raise, only flash that is not there.
 *
 * WHAT IS LOST, stated rather than glossed: a host asking for ATTEST_CERT on a
 * 32U4 gets AG_ERR_UNKNOWN_CMD and CAPS reports the feature bit clear, so it
 * learns the truth before it depends on the answer. ATTEST still works, and the
 * evidence frame still carries the firmware measurement and the nonce -- what
 * this part cannot do is wrap them in an X.509 certificate for a verifier that
 * wants DICE evidence in the spec's own extension.
 *
 * The honest alternative was to drop the measurement claim from the claims set,
 * which saves 388 bytes and would fit with eighteen to spare. Eighteen bytes is
 * not headroom, and evidence that does not say what it measured is not worth
 * the certificate around it.
 */
#define AG_FEATURE_ALIAS_CERT 0

/* Half the usable space, each. Usable flash is 28672 bytes, not 32768, because
 * Caterina owns the top 4 KB -- so the half is 14336 rather than 16384. RAM is
 * half of 2560.
 *
 * The budget is a gate, not an estimate: `make size-check` fails the build when
 * an image exceeds it, so flash creep is caught on the commit that causes it
 * rather than the day someone tries to add a feature and finds there is no
 * room. */
/* 32 KB of flash, with the top 4 KB spoken for. On this part that 4 KB is
 * CATERINA, the USB CDC bootloader a Leonardo ships with -- so flashing ag-boot
 * here REPLACES it, and with it the 1200-baud-touch reset that avrdude's avr109
 * programmer depends on. That is a real consequence and it is why this board is
 * not the one the bootloader work targets: recovering a part whose Caterina has
 * been replaced needs an ISP, not a USB cable. */
#define AG_FLASH_TOTAL       32768uL
#define AG_BOOT_SECTION_SIZE 4096uL

#define AG_FLASH_BUDGET 14336u
#define AG_RAM_BUDGET   1280u

/*
 * Register addresses.
 *
 * ONE definition, used by the device and by the host alike. The number is the
 * only source, and avr-libc CHECKS it: the asserts at the bottom of this file
 * compare every address against <avr/io.h> whenever this is compiled for AVR.
 * A transposed digit is a compile error on the firmware build, naming the
 * register.
 *
 * Every classic AVR lays out PINx, DDRx, PORTx consecutively, so one address
 * locates all three.
 */
#define AG_ADDR_PINB 0x23u  /* DDRB 0x24, PORTB 0x25 */
#define AG_ADDR_PINC 0x26u  /* DDRC 0x27, PORTC 0x28 */
#define AG_ADDR_PIND 0x29u  /* DDRD 0x2A, PORTD 0x2B */
#define AG_ADDR_PINE 0x2Cu  /* DDRE 0x2D, PORTE 0x2E */
#define AG_ADDR_PINF 0x2Fu  /* DDRF 0x30, PORTF 0x31 */

/* The two timers this build spends on PWM. Timer1 and Timer3 are both 16-bit
 * and both sit in the extended I/O space, so their TCCRnA and OCRnxL addresses
 * are memory addresses rather than I/O offsets.
 *
 * Timer4 is left alone on purpose. It is the 32U4's high-speed 10-bit timer and
 * its compare outputs need PWM4x enables and a TC4H high-byte write that the
 * generic ag_hal_pwm_write does not do -- a PWM descriptor pointing at OCR4A
 * would compile, link and then produce a duty cycle nobody asked for. That
 * costs pins 6 and 13 their PWM, which the table reports honestly as absent. */
#define AG_ADDR_TCCR1A 0x80u
#define AG_ADDR_OCR1AL 0x88u
#define AG_ADDR_OCR1BL 0x8Au
#define AG_ADDR_TCCR3A 0x90u
#define AG_ADDR_OCR3AL 0x98u

/* COM bit positions within TCCRnA. Constants rather than <avr/io.h> symbols so
 * the table compiles on the host, where that header does not exist -- and the
 * values are the same ones the datasheet gives, so what the tests check is what
 * the device writes. */
#define COM1A1_BIT 7u
#define COM1B1_BIT 5u
#define COM3A1_BIT 7u

/* The check. avr-libc is the authority on where these live; this asserts the
 * numbers above against it, so the host tests and the shipped image cannot
 * disagree about a single register. Costs nothing at run time and is the only
 * preprocessor conditional a board profile needs. */
#ifdef __AVR__
#include <avr/io.h>
_Static_assert(AG_ADDR_PINB   == _SFR_ADDR(PINB),   "PINB address");
_Static_assert(AG_ADDR_PINC   == _SFR_ADDR(PINC),   "PINC address");
_Static_assert(AG_ADDR_PIND   == _SFR_ADDR(PIND),   "PIND address");
_Static_assert(AG_ADDR_PINE   == _SFR_ADDR(PINE),   "PINE address");
_Static_assert(AG_ADDR_PINF   == _SFR_ADDR(PINF),   "PINF address");
_Static_assert(AG_ADDR_TCCR1A == _SFR_ADDR(TCCR1A), "TCCR1A address");
_Static_assert(AG_ADDR_OCR1AL == _SFR_ADDR(OCR1AL), "OCR1AL address");
_Static_assert(AG_ADDR_OCR1BL == _SFR_ADDR(OCR1BL), "OCR1BL address");
_Static_assert(AG_ADDR_TCCR3A == _SFR_ADDR(TCCR3A), "TCCR3A address");
_Static_assert(AG_ADDR_OCR3AL == _SFR_ADDR(OCR3AL), "OCR3AL address");
_Static_assert(COM1A1_BIT == COM1A1, "COM1A1 bit position");
_Static_assert(COM1B1_BIT == COM1B1, "COM1B1 bit position");
_Static_assert(COM3A1_BIT == COM3A1, "COM3A1 bit position");
/* The three consecutive-register assumptions the lookups rely on. Stated once
 * per family here because a board that broke one would fail silently: DDRx
 * writes would land in the wrong register on a part nobody had checked. */
_Static_assert(_SFR_ADDR(DDRF) == AG_ADDR_PINF + 1u, "PORTF trio is not consecutive");
_Static_assert(_SFR_ADDR(PORTF) == AG_ADDR_PINF + 2u, "PORTF trio is not consecutive");
_Static_assert(_SFR_ADDR(DDRE) == AG_ADDR_PINE + 1u, "PORTE trio is not consecutive");
/* MUX5 is what makes channels 8..13 reachable at all. A part without it would
 * silently convert the wrong pin for every channel above 7, so its absence must
 * be a build failure rather than a wrong reading. */
_Static_assert(MUX5 == 5, "MUX5 bit position: ADC channels above 7 are unreachable without it");
#endif

/* Which USART the firmware talks over.
 *
 * USART1, not USART0. This part's USART0 is the USB peripheral, and this
 * firmware speaks to the TTL pins rather than implementing CDC -- so a host
 * talks to it over an FTDI cable, not the native USB port.
 *
 * The register names are pasted from AG_UART_N in src/hal/avr/hal_uart.c; the
 * vector names are spelled out because avr-libc is not consistent about them
 * and a pasted name that does not exist compiles to an ordinary function
 * nothing calls -- a device that simply never answers.
 */
#define AG_UART_N          1
#define AG_UART_RX_VECT    USART1_RX_vect
#define AG_UART_UDRE_VECT  USART1_UDRE_vect

/* No tone on this part, and the reason is the silicon rather than a choice.
 *
 * The tone path in src/hal/avr/hal_tone.c drives Timer2 -- an 8-bit timer whose
 * CTC mode and prescaler table are not the 16-bit timers'. This part has no
 * Timer2 at all (its complement is 0, 1, 3 and the high-speed 4), so sharing
 * that code with Timer3 would sound a tone at the wrong frequency rather than
 * no tone, which is the failure that gets shipped and then argued about.
 *
 * Turning the feature off means the module is not compiled, CAPS reports tone
 * as absent, and a host learns the truth before it tries. A 16-bit tone path is
 * the change that would flip this back on.
 */
#define AG_FEATURE_TONE 0

/* The SPI pins' bit positions within port B. The HAL must set their direction
 * before the peripheral works, and it cannot ask the pin table for them:
 * the SPI lines are the hardware's, not logical pins a host names.
 * PB2 MOSI, PB1 SCK, PB0 SS -- pins 16, 15 and 17 on the Micro's map. */
#define AG_SPI_BIT_MOSI 2u
#define AG_SPI_BIT_SCK  1u
#define AG_SPI_BIT_SS   0u

#endif /* AG_BOARD_ATMEGA32U4_H */
