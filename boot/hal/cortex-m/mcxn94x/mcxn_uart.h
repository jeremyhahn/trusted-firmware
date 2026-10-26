/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * tfw-boot's polled LPUART on the MCXN947: the LP_FLEXCOMM unit's own
 * registers, as the application's hal_uart.c sets them, less its interrupt.
 * Each call takes the unit's base as a word pointer, so the host test
 * (test/test_mcxn_uart.c) hands it a register model; boot_uart.c hands it
 * LP_FLEXCOMM4 and does the clock, the reset and the pins around it. The
 * offsets and bits are soc_regs.h's.
 */
#ifndef TFW_BOOT_CM_MCXN94X_UART_H
#define TFW_BOOT_CM_MCXN94X_UART_H

#include <stdint.h>

#include "tfw/status.h"

/* The unit as an LPUART at baud from a clk_hz functional clock, receiver
 * and transmitter on, no interrupt. TFW_ERR_RANGE, with no register written,
 * for a rate lpuart_baud.c cannot divide to. */
tfw_status_t tfw_mcxn_uart_start(volatile uint32_t *fc, uint32_t clk_hz, uint32_t baud);

/* 1 and the byte when one has arrived, else 0. An overrun, noise or framing
 * flag is cleared on the way, so one bad byte does not stop the receiver. */
uint8_t tfw_mcxn_uart_getc(volatile uint32_t *fc, uint8_t *out);

/* Waits for room, then queues the byte. */
void tfw_mcxn_uart_putc(volatile uint32_t *fc, uint8_t byte);

/* Waits for the last byte to leave the shifter, then holds the LPUART in its
 * reset and gives the unit up as an LPUART. */
void tfw_mcxn_uart_stop(volatile uint32_t *fc);

#endif /* TFW_BOOT_CM_MCXN94X_UART_H */
