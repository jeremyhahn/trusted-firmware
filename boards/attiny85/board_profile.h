/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ATtiny85 -- 8 KB flash, 512 bytes SRAM, 512 bytes EEPROM, one 6-bit port.
 *
 * This part is not a smaller ATmega. It is a different shape, and four of the
 * assumptions the other three boards share are false here:
 *
 *   1. THERE IS NO USART. Not a second one, not a renumbered one -- none. The
 *      byte link is bit-banged in software off Timer1, at a baud this part can
 *      actually hold rather than the 115200 every other board uses.
 *   2. THERE IS NO TWI AND NO SPI PERIPHERAL. The part has a USI, which is a
 *      shift register with a clock, not either bus. src/hal/avr/hal_bus.c does
 *      not compile here, so I2C, SPI and 1-Wire are gated off by file
 *      selection and CAPS reports all three absent.
 *   3. TIMER1 IS NOT A MEGA TIMER. Its control register is TCCR1 with PWM1A
 *      and CS13:0 where a mega has TCCR1A with WGM and COM bits, so the
 *      generic ag_hal_pwm_write cannot drive it. It is spent on the software
 *      UART's bit clock here in any case.
 *   4. THE ADC REFERENCE ENCODING IS INVERTED. REFS2:1:0 = 000 is Vcc and 001
 *      is AREF; on a mega REFS1:0 = 01 is AVcc and 00 is AREF. A board that
 *      pasted the mega encoding would select an external reference while
 *      reporting Vcc, and every reading would be plausible and wrong. The
 *      handling is in src/hal/avr/hal_adc.c beside the MUX5 conditional, which
 *      is the same class of fact about the same peripheral.
 *
 * WHETHER THIS BOARD SHIPS AN IMAGE is answered by docs/boards/attiny85.md,
 * with the measurement. The descriptor below is correct either way: the pin
 * table is data, and `make board-sweep` verifies it whether or not an image
 * links.
 *
 * A board profile states only its deviations from include/ag/config.h.
 */
#ifndef AG_BOARD_ATTINY85_H
#define AG_BOARD_ATTINY85_H

#define AG_BOARD_NAME "attiny85"
#define AG_BOARD_ID   0x04u   /* 0x01 328P, 0x02 2560, 0x03 32U4 */

/*
 * Pin numbering: PB0..PB5 are logical pins 0..5, and there is nothing else.
 *
 * One port, six bits, no holes and no second numbering scheme to reconcile --
 * which is the one respect in which this part is simpler than every other board
 * in the tree. The numbers match what the ATTinyCore and Digispark cores use,
 * so a silkscreen that says P3 is pin 3 here.
 *
 * Pin 5 is /RESET. It is a real port bit and a real ADC channel, and it becomes
 * a general-purpose pin only when RSTDISBL is programmed -- which also makes
 * the part unprogrammable over ISP. It is therefore marked AG_PIN_RESERVED
 * rather than omitted: a host is told the pin exists, where it is, and that
 * this build does not expect it to be driven.
 */
#define AG_PIN_COUNT  6u
#define AG_PORT_COUNT 1u    /* B=0, and that is the whole list */

/*
 * NO PWM IN THIS BUILD, and the reason is the silicon rather than the budget.
 *
 * Timer0's OC0A and OC0B are spent on the millisecond tick, exactly as on every
 * other board. Timer1's OC1A and OC1B are spent on the software UART's bit
 * clock, which is what this part has instead of a USART -- so every
 * output-compare unit on the part is already doing something the firmware
 * cannot do without.
 *
 * Even with a free timer the generic path would not work: ag_hal_pwm_write sets
 * WGM00 and a COM bit in TCCRnA, and Timer1's control register here is TCCR1,
 * whose bit 0 is CS10 and which needs PWM1A rather than a waveform-mode bit. A
 * descriptor pointing at it would compile, link, and start the timer at clk/1
 * while connecting nothing. So the table is empty and the pins that HAVE a
 * compare unit are marked AG_PWM_RSVD, which tells a host the difference
 * between "no PWM on this pin" and "this build spent it".
 *
 * A 8-bit Timer1 PWM path in the HAL, plus a hardware USART this part does not
 * have, is what would flip this back on. Neither is a budget question.
 */
#define AG_PWM_COUNT  0u

/*
 * ADC channels. ADC0 is on PB5, ADC1 on PB2, ADC2 on PB4 and ADC3 on PB3 --
 * note that the channel numbers do NOT run with the port bits, which is the
 * kind of mapping the descriptor test exists to pin down.
 *
 * AG_ADC_COUNT is the HAL's range-check bound, so it is one past the highest
 * real channel. Channels 4..11 in ADMUX select differential pairs, the bandgap
 * and ground; none of them is a pin, so none is reachable.
 */
#define AG_ADC_COUNT  4u

#define AG_EEPROM_SIZE 512u

#define AG_TICK_TIMER 0u
/* Names the timer tone WOULD use. This part has no Timer2, so AG_FEATURE_TONE
 * is 0 below and src/hal/avr/hal_tone.c -- which asserts AG_TONE_TIMER == 2 --
 * is not compiled at all. Stating 1 here would be a lie about the silicon; 2
 * would make the assert pass for a timer that does not exist. */
#define AG_TONE_TIMER 0u

/*
 * SIZING, and this is where 512 bytes of SRAM stops being an abstraction.
 *
 * At the tree's defaults -- 64-byte payload, 64-byte rings, 8 scheduler slots
 * -- the statics come to roughly 430 bytes on a part that has 512, which leaves
 * no stack. Every number below is a deviation forced by that, and each one is a
 * capability reduction stated rather than a tuning preference:
 *
 *   AG_MAX_PAYLOAD 32   The largest EEPROM_WRITE or PING this part accepts.
 *                       Drives both frame buffers and the transport's payload
 *                       copy, so it is worth three bytes of SRAM per byte of
 *                       payload. 64 -> 32 buys back 96 bytes.
 *   AG_SCHED_SLOTS 4    One timed level per slot, six bytes each. There are
 *                       only six pins and two of them are the UART, so four
 *                       slots is the whole usable pin count rather than an
 *                       arbitrary cut.
 *   AG_UART_*_RING 16   The soft UART's rings. At 19200 baud a byte takes
 *                       521 us and the superloop turns over far faster than
 *                       that, so 16 bytes is many poll intervals of slack.
 *                       They must stay powers of two and <= 256; see
 *                       src/hal/avr/hal_uart_soft.c.
 */
#define AG_MAX_PAYLOAD  32u
#define AG_SCHED_SLOTS  4u
#define AG_UART_RX_RING 16u
#define AG_UART_TX_RING 16u

/*
 * UNATTESTED, and this is the capability the part actually loses.
 *
 * ATTEST is off. Not because the code is wrong here but because it does not
 * fit: SHA-256 plus HMAC plus the evidence encoder is over two kilobytes of an
 * eight-kilobyte part, and the element tier that would carry the key is an I2C
 * device on a part with no I2C. A host asking for ATTEST gets
 * AG_ERR_UNKNOWN_CMD and CAPS reports the feature bit clear, so it learns
 * before it depends on the answer.
 *
 * What that means in practice: this part can be a peripheral behind a device
 * that can attest. It cannot be one itself, and no amount of gating changes
 * that, because the honest version of the feature needs a key store this
 * silicon does not have.
 */
#define AG_FEATURE_ATTEST     0
#define AG_FEATURE_ALIAS_CERT 0

/* No Timer2 on this part at all, so the tone path -- which is Timer2's CTC
 * mode and prescaler table -- has nothing to drive. Gated by file selection:
 * src/caps/tone.c and src/hal/avr/hal_tone.c are not compiled, and CAPS
 * reports tone absent. */
#define AG_FEATURE_TONE 0

/*
 * NO PWM, NO I2C, NO SPI AND NO 1-WIRE, all gated by file selection.
 *
 * The first three are silicon: see AG_PWM_COUNT above for PWM, and for the
 * buses, this part has a USI rather than a TWI and an SPI. A USI can be driven
 * as either with enough software, but it is a different peripheral with a
 * different register set -- src/hal/avr/hal_bus.c references TWCR, TWDR, SPCR
 * and SPDR and does not compile here.
 *
 * 1-WIRE IS THE ONE THAT IS NOT SILICON, and it is worth saying plainly. The
 * 1-Wire path is bit-banged on an ordinary GPIO and would work on this part
 * unchanged. It is off because it shares a translation unit with the TWI and
 * SPI drivers, and the gate is whole-file selection. Splitting hal_bus.c into
 * its three drivers is the change that would give this part 1-Wire back; until
 * someone needs it, CAPS reports it absent, which is the truth about this
 * build even though it is not the truth about the part.
 */
#define AG_FEATURE_PWM     0
#define AG_FEATURE_I2C     0
#define AG_FEATURE_SPI     0
#define AG_FEATURE_ONEWIRE 0

/*
 * THE SOFTWARE UART.
 *
 * AG_UART_SOFT selects src/hal/avr/hal_uart_soft.c in place of hal_uart.c. It
 * is a file-selection gate like every other, and it is why this profile has no
 * AG_UART_N and no vector names: there is no USART to number.
 *
 * BAUD: 19200, not the tree's 115200, and the arithmetic is the argument.
 *
 * At 8 MHz a bit at 115200 is 69 CPU cycles. An ISR entry, the sample and the
 * return do not fit in 69 cycles with the millisecond tick also running, so
 * 115200 here is not a tight link, it is a link that does not work. 19200 gives
 * 417 cycles a bit, and the error budget closes:
 *
 *   divisor        Timer1 at CK/2 is 4 MHz; a bit wants 208.33 ticks and gets
 *                  208, so the bit time is 52.000 us against 52.0833 nominal:
 *                  -0.160%. Over the 9.5 bits to the middle of the stop bit
 *                  that is 1.5% of a bit.
 *   oscillator     The internal RC is +/-2% over voltage and temperature after
 *                  factory calibration. 19% of a bit, and it DOMINATES -- the
 *                  divisor error is noise beside it, which is why picking a
 *                  baud with a smaller divisor error would buy nothing.
 *   ISR jitter     The tick ISR and the TX ISR each delay an RX sample by at
 *                  most ~40 cycles: 10% of a bit at 19200, 19% at 38400.
 *
 * Worst case about 30% of a bit against a 50% sampling window. At 38400 the
 * same sum is about 40%, which works on a bench and not over temperature, and
 * the part that would give the margin back is a crystal -- so a board with one
 * can raise this, and a board on the internal RC should not.
 *
 * The HAL asserts the divisor error and the counter range at compile time from
 * these three numbers, so a board that changes F_CPU or the baud finds out here
 * rather than on a scope.
 */
#define AG_UART_SOFT     1
#define AG_UART_BAUD     19200UL
#define AG_UART_SOFT_CS  2u   /* TCCR1 CS13:0 -- CK/2, i.e. divisor 1 << (CS-1) */

/* Which pins the link occupies, as bits within PORTB.
 *
 * PB0 and PB1 and not a pair with an ADC channel on it: PB2, PB3 and PB4 carry
 * ADC1, ADC3 and ADC2, and there are only three usable analog inputs on this
 * part. Spending one of them on a serial line nobody can move would be the
 * expensive choice on a part this small.
 *
 * RX must be on a pin with a pin-change channel, because that is how a start
 * bit is detected. On this part PBn is PCINTn for every n, which the assert
 * below states rather than assumes. */
#define AG_UART_SOFT_RX_BIT 0u   /* PB0, PCINT0 */
#define AG_UART_SOFT_TX_BIT 1u   /* PB1 */

/*
 * Register addresses. ONE definition, checked against <avr/io.h> below.
 *
 * Every classic AVR lays out PINx, DDRx, PORTx consecutively, so one address
 * locates all three -- and on this part that one address is the entire port
 * table.
 *
 * Note it is 0x36 and not 0x23. The ATtiny85's ports live in the LOW I/O space
 * at I/O offset 0x16, and _SFR_ADDR adds 0x20 to reach the memory-mapped
 * alias. The descriptor stores the memory address, which is what
 * src/hal/avr/hal_gpio.c's _SFR_MEM8 wants and what makes the same GPIO code
 * work from an ATtiny to an ATmega2560.
 */
#define AG_ADDR_PINB 0x36u  /* DDRB 0x37, PORTB 0x38 */

/* Flash and RAM geometry.
 *
 * There is NO BOOT SECTION. The ATtiny85 has no boot-loader section and no
 * BOOTRST fuse -- SPM works only from... nowhere an application can use it
 * safely, because there is no NRWW section to execute from during a write. So
 * ag-boot cannot exist on this part, AG_BOOT_SECTION_SIZE is 0, and the whole
 * 8 KB is application space.
 *
 * That also removes the software tier's key store: src/crypto/soft/uds_flash.c
 * reads the UDS out of the boot section behind lock bits, and there is no boot
 * section to put one in. It is a second, independent reason attestation is off
 * here, and it is the one that does not move with the budget. */
#define AG_FLASH_TOTAL       8192uL
#define AG_BOOT_SECTION_SIZE 0uL

/* Half the flash and half the SRAM, the same rule as every other board, and
 * the same kind of gate: `make firmware` fails when an image exceeds one. On a
 * part this size the RAM half is the binding constraint and it is meant to be
 * -- 256 bytes of statics leaves 256 bytes of stack, and the firmware's
 * deepest path is a dispatch through a capability handler into the HAL. */
#define AG_FLASH_BUDGET 6144u
#define AG_RAM_BUDGET   256u

/* The check. avr-libc is the authority on where these live; this asserts the
 * numbers above against it, so the host tests and the shipped image cannot
 * disagree about a single register. */
#ifdef __AVR__
#include <avr/io.h>
_Static_assert(AG_ADDR_PINB == _SFR_ADDR(PINB), "PINB address");
/* The consecutive-register assumption every lookup in the tree relies on. On a
 * part with one port, getting this wrong would put every DDRB write into PORTB
 * and every pin would read back as whatever its pull-up was set to. */
_Static_assert(_SFR_ADDR(DDRB)  == AG_ADDR_PINB + 1u, "PORTB trio is not consecutive");
_Static_assert(_SFR_ADDR(PORTB) == AG_ADDR_PINB + 2u, "PORTB trio is not consecutive");
/* The software UART's RX pin must have a pin-change channel, or a start bit is
 * undetectable and the device simply never answers. */
_Static_assert(AG_UART_SOFT_RX_BIT == PCINT0, "the soft UART's RX pin has no PCINT channel");
/* REFS2 is what makes this part's reference encoding different from a mega's.
 * Its absence would mean hal_adc.c had taken the mega path and every reading
 * would be against the wrong reference. */
_Static_assert(REFS2 == 4, "ADMUX REFS2: this part's reference encoding is not the mega's");
/* This part has no Timer2, which is the whole reason AG_FEATURE_TONE is 0. If a
 * future avr-libc ever defined one, the profile should be revisited rather than
 * left quietly conservative. */
#ifdef TCCR2A
#error "this part was supposed to have no Timer2"
#endif
#endif

#endif /* AG_BOARD_ATTINY85_H */
