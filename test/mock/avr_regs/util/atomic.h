/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * <util/atomic.h> for the host, as wide as src/hal/avr/hal_uart_soft.c needs
 * (#163). ATOMIC_BLOCK runs its body once between tfw_avr_atomic_enter() and
 * tfw_avr_atomic_leave(), which the suite defines, so the suite can see which
 * register accesses fall inside the block. Only on the -I of the suites that
 * test those units; the AVR build uses avr-libc's.
 */
#ifndef TFW_TEST_UTIL_ATOMIC_H
#define TFW_TEST_UTIL_ATOMIC_H

#define ATOMIC_RESTORESTATE 0

int tfw_avr_atomic_enter(void);
int tfw_avr_atomic_leave(void);

#define ATOMIC_BLOCK(type) \
    for (int tfw_atomic_once_ = ((void)(type), tfw_avr_atomic_enter()); tfw_atomic_once_; \
         tfw_atomic_once_ = tfw_avr_atomic_leave())

#endif
