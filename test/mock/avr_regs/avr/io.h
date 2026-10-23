/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <avr/io.h> for the host, as wide as the AVR units under test need and no
 * wider (#163): the GPIO ports and the SPI peripheral for
 * boot/hal/avr/boot_ota_store_avr.c, USART0 and USART1 for
 * src/hal/avr/hal_uart.c, the ATtiny85's Timer1 and pin change for
 * src/hal/avr/hal_uart_soft.c, _SFR_MEM8 for src/hal/avr/hal_gpio.c, the ADC
 * for src/hal/avr/hal_adc.c, _SFR_MEM16 and WGM00 for src/hal/avr/hal_pwm.c,
 * Timer2 for src/hal/avr/hal_tone.c, the TWI and SPCR's DORD and CPHA for
 * src/hal/avr/hal_bus.c, CLKPR for src/hal/avr/hal_sys.c, Timer0 and TIMSK0
 * for src/hal/avr/hal_tick.c, MCUSR for src/hal/avr/hal_early.c, SMCR for
 * boot/hal/avr/recovery_halt.c, SREG and WDTCSR for boot/hal/avr/boot_hal_avr.c
 * (whose suite's -D defines EIND on the part that has it). Every register
 * is an access through tfw_avr_reg(), which the suite defines, so the suite
 * sees each write land and can model the SPI exchange: SPDR written, SPSR
 * polled until SPIF, SPDR read back. Only on the -I of the suite that tests
 * that unit; the AVR build uses avr-libc's.
 */
#ifndef TFW_TEST_AVR_IO_H
#define TFW_TEST_AVR_IO_H

#include <stdint.h>

enum {
    TFW_AVR_PORTA, TFW_AVR_PORTB, TFW_AVR_PORTC, TFW_AVR_PORTD, TFW_AVR_PORTE, TFW_AVR_PORTF,
    TFW_AVR_PORTG, TFW_AVR_PORTH, TFW_AVR_PORTI, TFW_AVR_PORTJ, TFW_AVR_PORTK, TFW_AVR_PORTL,
    TFW_AVR_DDRA, TFW_AVR_DDRB, TFW_AVR_DDRC, TFW_AVR_DDRD, TFW_AVR_DDRE, TFW_AVR_DDRF,
    TFW_AVR_DDRG, TFW_AVR_DDRH, TFW_AVR_DDRI, TFW_AVR_DDRJ, TFW_AVR_DDRK, TFW_AVR_DDRL,
    TFW_AVR_SPCR, TFW_AVR_SPSR, TFW_AVR_SPDR,
    /* src/hal/avr/hal_uart.c: USART0 and USART1. */
    TFW_AVR_UDR0, TFW_AVR_UCSR0A, TFW_AVR_UCSR0B, TFW_AVR_UCSR0C, TFW_AVR_UBRR0H, TFW_AVR_UBRR0L,
    TFW_AVR_UDR1, TFW_AVR_UCSR1A, TFW_AVR_UCSR1B, TFW_AVR_UCSR1C, TFW_AVR_UBRR1H, TFW_AVR_UBRR1L,
    /* src/hal/avr/hal_uart_soft.c: the ATtiny85's PINB, Timer1 and pin change. */
    TFW_AVR_PINB, TFW_AVR_TCCR1, TFW_AVR_TCNT1, TFW_AVR_OCR1A, TFW_AVR_OCR1B, TFW_AVR_OCR1C,
    TFW_AVR_TIMSK, TFW_AVR_TIFR, TFW_AVR_GIMSK, TFW_AVR_GIFR, TFW_AVR_PCMSK,
    /* src/hal/avr/hal_adc.c. */
    TFW_AVR_ADMUX, TFW_AVR_ADCSRA, TFW_AVR_ADCSRB, TFW_AVR_ADCL, TFW_AVR_ADCH,
    /* src/hal/avr/hal_tone.c: Timer2. */
    TFW_AVR_TCCR2A, TFW_AVR_TCCR2B, TFW_AVR_OCR2A,
    /* src/hal/avr/hal_bus.c: the TWI. */
    TFW_AVR_TWBR, TFW_AVR_TWSR, TFW_AVR_TWCR, TFW_AVR_TWDR,
    /* src/hal/avr/hal_sys.c: the clock prescaler. */
    TFW_AVR_CLKPR,
    /* src/hal/avr/hal_tick.c: Timer0, the mega's own mask register. */
    TFW_AVR_TCCR0A, TFW_AVR_TCCR0B, TFW_AVR_OCR0A, TFW_AVR_TIMSK0,
    /* src/hal/avr/hal_early.c: the reset flags. */
    TFW_AVR_MCUSR,
    /* boot/hal/avr/recovery_halt.c: the sleep mode. */
    TFW_AVR_SMCR,
    /* boot/hal/avr/boot_hal_avr.c: the status register, the watchdog and,
     * where the suite's -D names it, EIND. */
    TFW_AVR_SREG, TFW_AVR_WDTCSR, TFW_AVR_EIND,
    TFW_AVR_REGS
};

volatile uint8_t *tfw_avr_reg(unsigned id);

/* A register by its data-memory address, as src/hal/avr/hal_gpio.c reaches a
 * port from the board's pin table: the suite that uses it defines it. */
volatile uint8_t *tfw_avr_mem(unsigned addr);
#define _SFR_MEM8(addr) (*tfw_avr_mem((unsigned)(addr)))

/* A 16-bit register pair by address, as src/hal/avr/hal_pwm.c writes an OCR:
 * one word access, which the suite that uses it defines. */
volatile uint16_t *tfw_avr_mem16(unsigned addr);
#define _SFR_MEM16(addr) (*tfw_avr_mem16((unsigned)(addr)))

#define PORTA (*tfw_avr_reg(TFW_AVR_PORTA))
#define PORTB (*tfw_avr_reg(TFW_AVR_PORTB))
#define PORTC (*tfw_avr_reg(TFW_AVR_PORTC))
#define PORTD (*tfw_avr_reg(TFW_AVR_PORTD))
#define PORTE (*tfw_avr_reg(TFW_AVR_PORTE))
#define PORTF (*tfw_avr_reg(TFW_AVR_PORTF))
#define PORTG (*tfw_avr_reg(TFW_AVR_PORTG))
#define PORTH (*tfw_avr_reg(TFW_AVR_PORTH))
#define PORTJ (*tfw_avr_reg(TFW_AVR_PORTJ))
#define PORTK (*tfw_avr_reg(TFW_AVR_PORTK))
#define PORTL (*tfw_avr_reg(TFW_AVR_PORTL))
#define DDRA  (*tfw_avr_reg(TFW_AVR_DDRA))
#define DDRB  (*tfw_avr_reg(TFW_AVR_DDRB))
#define DDRC  (*tfw_avr_reg(TFW_AVR_DDRC))
#define DDRD  (*tfw_avr_reg(TFW_AVR_DDRD))
#define DDRE  (*tfw_avr_reg(TFW_AVR_DDRE))
#define DDRF  (*tfw_avr_reg(TFW_AVR_DDRF))
#define DDRG  (*tfw_avr_reg(TFW_AVR_DDRG))
#define DDRH  (*tfw_avr_reg(TFW_AVR_DDRH))
#define DDRJ  (*tfw_avr_reg(TFW_AVR_DDRJ))
#define DDRK  (*tfw_avr_reg(TFW_AVR_DDRK))
#define DDRL  (*tfw_avr_reg(TFW_AVR_DDRL))
#define SPCR  (*tfw_avr_reg(TFW_AVR_SPCR))
#define SPSR  (*tfw_avr_reg(TFW_AVR_SPSR))
#define SPDR  (*tfw_avr_reg(TFW_AVR_SPDR))

/* The bit numbers avr-libc gives every part with this SPI. */
#define SPE  6
#define MSTR 4
#define SPIF 7

#define _BV(bit) (1 << (bit))

#define UDR0    (*tfw_avr_reg(TFW_AVR_UDR0))
#define UCSR0A  (*tfw_avr_reg(TFW_AVR_UCSR0A))
#define UCSR0B  (*tfw_avr_reg(TFW_AVR_UCSR0B))
#define UCSR0C  (*tfw_avr_reg(TFW_AVR_UCSR0C))
#define UBRR0H  (*tfw_avr_reg(TFW_AVR_UBRR0H))
#define UBRR0L  (*tfw_avr_reg(TFW_AVR_UBRR0L))
#define UDR1    (*tfw_avr_reg(TFW_AVR_UDR1))
#define UCSR1A  (*tfw_avr_reg(TFW_AVR_UCSR1A))
#define UCSR1B  (*tfw_avr_reg(TFW_AVR_UCSR1B))
#define UCSR1C  (*tfw_avr_reg(TFW_AVR_UCSR1C))
#define UBRR1H  (*tfw_avr_reg(TFW_AVR_UBRR1H))
#define UBRR1L  (*tfw_avr_reg(TFW_AVR_UBRR1L))

/* The USART bit numbers, the same on USART0 and USART1 of every part here. */
#define RXC0   7
#define TXC0   6
#define UDRE0  5
#define U2X0   1
#define RXCIE0 7
#define UDRIE0 5
#define RXEN0  4
#define TXEN0  3
#define UCSZ01 2
#define UCSZ00 1
#define RXC1   7
#define TXC1   6
#define UDRE1  5
#define U2X1   1
#define RXCIE1 7
#define UDRIE1 5
#define RXEN1  4
#define TXEN1  3
#define UCSZ11 2
#define UCSZ10 1

#define PINB   (*tfw_avr_reg(TFW_AVR_PINB))
#define TCCR1  (*tfw_avr_reg(TFW_AVR_TCCR1))
#define TCNT1  (*tfw_avr_reg(TFW_AVR_TCNT1))
#define OCR1A  (*tfw_avr_reg(TFW_AVR_OCR1A))
#define OCR1B  (*tfw_avr_reg(TFW_AVR_OCR1B))
#define OCR1C  (*tfw_avr_reg(TFW_AVR_OCR1C))
#define TIMSK  (*tfw_avr_reg(TFW_AVR_TIMSK))
#define TIFR   (*tfw_avr_reg(TFW_AVR_TIFR))
#define GIMSK  (*tfw_avr_reg(TFW_AVR_GIMSK))
#define GIFR   (*tfw_avr_reg(TFW_AVR_GIFR))
#define PCMSK  (*tfw_avr_reg(TFW_AVR_PCMSK))

/* The ATtiny85's bit numbers (datasheet 2586Q, sections 9.3 and 12.3). */
#define CTC1   7
#define OCIE1A 6
#define OCIE1B 5
#define OCF1A  6
#define OCF1B  5
#define PCIE   5
#define PCIF   5

/* The ADC, whose bits sit alike on the mega and the ATtiny85. REFS2 (the
 * tiny's) and MUX5 (the 2560's and 32U4's) are per part, so a suite's make
 * rule passes each as a -D for the part it builds, as avr-libc would. */
#define ADMUX  (*tfw_avr_reg(TFW_AVR_ADMUX))
#define ADCSRA (*tfw_avr_reg(TFW_AVR_ADCSRA))
#define ADCSRB (*tfw_avr_reg(TFW_AVR_ADCSRB))
#define ADCL   (*tfw_avr_reg(TFW_AVR_ADCL))
#define ADCH   (*tfw_avr_reg(TFW_AVR_ADCH))
#define REFS1  7
#define REFS0  6
#define ADEN   7
#define ADSC   6
#define ADPS2  2
#define ADPS1  1
#define ADPS0  0

/* Fast PWM 8-bit's low WGM bit, bit 0 of TCCRnA on every timer here. */
#define WGM00  0

/* Timer2 in CTC, toggling OC2A, clk/64: the 328P's and the 2560's bits. */
#define TCCR2A (*tfw_avr_reg(TFW_AVR_TCCR2A))
#define TCCR2B (*tfw_avr_reg(TFW_AVR_TCCR2B))
#define OCR2A  (*tfw_avr_reg(TFW_AVR_OCR2A))
#define COM2A0 6
#define WGM21  1
#define CS22   2

/* The TWI, and SPCR's mode and bit-order bits, as src/hal/avr/hal_bus.c
 * drives them: the same on every mega here. */
#define TWBR  (*tfw_avr_reg(TFW_AVR_TWBR))
#define TWSR  (*tfw_avr_reg(TFW_AVR_TWSR))
#define TWCR  (*tfw_avr_reg(TFW_AVR_TWCR))
#define TWDR  (*tfw_avr_reg(TFW_AVR_TWDR))
#define TWINT 7
#define TWEA  6
#define TWSTA 5
#define TWSTO 4
#define TWEN  2
#define DORD  5
#define CPHA  2

#define CLKPR  (*tfw_avr_reg(TFW_AVR_CLKPR))
#define CLKPCE 7

#define TCCR0A (*tfw_avr_reg(TFW_AVR_TCCR0A))
#define TCCR0B (*tfw_avr_reg(TFW_AVR_TCCR0B))
#define OCR0A  (*tfw_avr_reg(TFW_AVR_OCR0A))
#define TIMSK0 (*tfw_avr_reg(TFW_AVR_TIMSK0))
#define WGM01  1
#define CS01   1
#define CS00   0
#define OCIE0A 1

#define MCUSR (*tfw_avr_reg(TFW_AVR_MCUSR))
#define PORF  0
#define EXTRF 1
#define BORF  2
#define WDRF  3

#define SMCR (*tfw_avr_reg(TFW_AVR_SMCR))

#define SREG   (*tfw_avr_reg(TFW_AVR_SREG))
#define WDTCSR (*tfw_avr_reg(TFW_AVR_WDTCSR))
#define WDP0   0
#define WDE    3
#define WDCE   4
#define WDP3   5

#endif
