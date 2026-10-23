/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What the simavr harness listens on, taken from the board profile it is built
 * with, so one harness source serves the 2560, the 328P and the 32U4.
 *
 * The store's select is TFW_OTA_STORE_CS_IO, a port letter and a bit, which
 * tfw-boot's store HAL pastes into register names
 * (boot/hal/avr/boot_ota_store_avr.c). The harness needs the letter as the
 * character simavr names the port by: D8 is PH5 on the 2560, PB0 on the 328P
 * and PB4 on the 32U4.
 *
 * The UART is TFW_UART_N, the USART the application speaks on: USART0 on the
 * 2560 and the 328P, USART1 on the 32U4, which has no USART0. simavr names a
 * UART by its digit. The harness paces its bytes on UCSRnA's RXC bit, read by
 * data-space address: 0xC0 for USART0 on the 328P and the 2560, then 8 bytes
 * a USART up to USART2 (UCSR1A 0xC8 on the 2560 and the 32U4), as each data
 * sheet's register summary has it. The 2560's USART3 sits elsewhere, at 0x130,
 * and no board speaks on it.
 */
#ifndef TFW_BOOT_HARNESS_IO_H
#define TFW_BOOT_HARNESS_IO_H

#include "tfw/config.h"

#define HARNESS_IO_PORT_(p, b)  ((char)(#p)[0])
#define HARNESS_IO_BIT_(p, b)   (b)
#define HARNESS_IO_PORT(io)     HARNESS_IO_PORT_(io)
#define HARNESS_IO_BIT(io)      HARNESS_IO_BIT_(io)

#define STORE_CS_PORT  HARNESS_IO_PORT(TFW_OTA_STORE_CS_IO)
#define STORE_CS_BIT   HARNESS_IO_BIT(TFW_OTA_STORE_CS_IO)

#define HARNESS_UART   ('0' + TFW_UART_N)
#define HARNESS_UCSRA  (0x00C0u + 8u * (unsigned)(TFW_UART_N))
#define HARNESS_RXC    0x80u

_Static_assert(TFW_UART_N >= 0 && TFW_UART_N <= 2, "UCSRnA is 0xC0 + 8n only for USART0 to USART2");

#endif
