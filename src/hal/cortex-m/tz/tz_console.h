/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 images' console (#590): a polled CMSDK UART where the board's map
 * names one (TZ_CONSOLE_CMSDK, mps2), nothing elsewhere. Both worlds print
 * on it; it sits in the Non-secure peripheral region, so a secure image's
 * write is a Non-secure transaction, as the PPC the secure start-up opened
 * requires.
 */
#ifndef TFW_TZ_CONSOLE_H
#define TFW_TZ_CONSOLE_H

#include <stdint.h>

#ifdef TZ_CONSOLE_CMSDK
#define TZ_UART ((volatile uint32_t *)(uintptr_t)TZ_CONSOLE_CMSDK)
#endif

static inline void tz_console_init(void)
{
#ifdef TZ_UART
    TZ_UART[4] = 16u;   /* BAUDDIV */
    TZ_UART[2] = 1u;    /* CTRL: TX enable */
#endif
}

static inline void tz_putc(char c)
{
#ifdef TZ_UART
    while (TZ_UART[1] & 1u) {   /* STATE: TX full */
    }
    TZ_UART[0] = (uint8_t)c;
#else
    (void)c;
#endif
}

static inline void tz_puts(const char *s)
{
    while (*s)
        tz_putc(*s++);
}

static inline void tz_puthex(uint32_t v)
{
    int i;
    tz_puts("0x");
    for (i = 28; i >= 0; i -= 4)
        tz_putc("0123456789abcdef"[(v >> i) & 0xfu]);
}

#endif
