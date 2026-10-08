/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The USART register names, pasted from the board's TFW_UART_N.
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
#ifndef TFW_AVR_USART_REGS_H
#define TFW_AVR_USART_REGS_H

#define TFW_CAT_(a, b)      a##b
#define TFW_CAT(a, b)       TFW_CAT_(a, b)
/* Three-way paste in one step. Nesting TFW_CAT inside itself does NOT work:
 * ## suppresses expansion of its own operands, so the outer paste sees the
 * literal text "TFW_CAT(UCSR, TFW_UART_N)" and tries to glue ")" to "A". */
#define TFW_CAT3_(a, b, c)  a##b##c
#define TFW_CAT3(a, b, c)   TFW_CAT3_(a, b, c)

#define TFW_UDR    TFW_CAT(UDR,  TFW_UART_N)
#define TFW_UCSRA  TFW_CAT3(UCSR, TFW_UART_N, A)
#define TFW_UCSRB  TFW_CAT3(UCSR, TFW_UART_N, B)
#define TFW_UCSRC  TFW_CAT3(UCSR, TFW_UART_N, C)
#define TFW_UBRRH  TFW_CAT3(UBRR, TFW_UART_N, H)
#define TFW_UBRRL  TFW_CAT3(UBRR, TFW_UART_N, L)
#define TFW_U2X    TFW_CAT(U2X,   TFW_UART_N)
#define TFW_UCSZ0  TFW_CAT3(UCSZ, TFW_UART_N, 0)
#define TFW_UCSZ1  TFW_CAT3(UCSZ, TFW_UART_N, 1)
#define TFW_RXEN   TFW_CAT(RXEN,  TFW_UART_N)
#define TFW_TXEN   TFW_CAT(TXEN,  TFW_UART_N)
#define TFW_RXCIE  TFW_CAT(RXCIE, TFW_UART_N)
#define TFW_UDRIE  TFW_CAT(UDRIE, TFW_UART_N)
#define TFW_RXC    TFW_CAT(RXC,   TFW_UART_N)
#define TFW_UDRE   TFW_CAT(UDRE,  TFW_UART_N)
#define TFW_TXC    TFW_CAT(TXC,   TFW_UART_N)

#endif /* TFW_AVR_USART_REGS_H */
