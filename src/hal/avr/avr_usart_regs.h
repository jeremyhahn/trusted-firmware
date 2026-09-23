/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The USART register names, pasted from the board's AG_UART_N.
 *
 * Extracted here because TWO images need them and there must not be two copies:
 * the application's interrupt-driven UART in hal_uart.c, and the bootloader's
 * polled one in boot/hal/avr. Two copies of a register-name paste is two places
 * a board that moved to USART1 has to be remembered, and the one that is
 * forgotten does not fail to build -- it builds against a USART that is not
 * wired to anything.
 *
 * WHICH USART, and why this is not a literal. Three parts, three answers. The
 * 328P and the Mega talk over USART0; the 32U4's TTL pins are USART1, because
 * its USART0 is the USB peripheral and this firmware does not implement CDC.
 *
 * The interrupt VECTOR names are NOT pasted and the board gives them
 * explicitly, because avr-libc is not consistent: the 328P spells it
 * USART_RX_vect with no index, the Mega spells the same interrupt
 * USART0_RX_vect. A misspelled ISR is not a link error -- it compiles to an
 * ordinary function nothing calls -- so the symptom is a device that never
 * answers. That reasoning belongs with the vectors, in the board profile.
 *
 * This file lives under src/hal/avr, which is one of the places a target
 * conditional is permitted. It contains none: it is paste macros only.
 */
#ifndef AG_AVR_USART_REGS_H
#define AG_AVR_USART_REGS_H

#define AG_CAT_(a, b)      a##b
#define AG_CAT(a, b)       AG_CAT_(a, b)
/* Three-way paste in one step. Nesting AG_CAT inside itself does NOT work:
 * ## suppresses expansion of its own operands, so the outer paste sees the
 * literal text "AG_CAT(UCSR, AG_UART_N)" and tries to glue ")" to "A". */
#define AG_CAT3_(a, b, c)  a##b##c
#define AG_CAT3(a, b, c)   AG_CAT3_(a, b, c)

#define AG_UDR    AG_CAT(UDR,  AG_UART_N)
#define AG_UCSRA  AG_CAT3(UCSR, AG_UART_N, A)
#define AG_UCSRB  AG_CAT3(UCSR, AG_UART_N, B)
#define AG_UCSRC  AG_CAT3(UCSR, AG_UART_N, C)
#define AG_UBRRH  AG_CAT3(UBRR, AG_UART_N, H)
#define AG_UBRRL  AG_CAT3(UBRR, AG_UART_N, L)
#define AG_U2X    AG_CAT(U2X,   AG_UART_N)
#define AG_UCSZ0  AG_CAT3(UCSZ, AG_UART_N, 0)
#define AG_UCSZ1  AG_CAT3(UCSZ, AG_UART_N, 1)
#define AG_RXEN   AG_CAT(RXEN,  AG_UART_N)
#define AG_TXEN   AG_CAT(TXEN,  AG_UART_N)
#define AG_RXCIE  AG_CAT(RXCIE, AG_UART_N)
#define AG_UDRIE  AG_CAT(UDRIE, AG_UART_N)
#define AG_RXC    AG_CAT(RXC,   AG_UART_N)
#define AG_UDRE   AG_CAT(UDRE,  AG_UART_N)
#define AG_TXC    AG_CAT(TXC,   AG_UART_N)

#endif /* AG_AVR_USART_REGS_H */
