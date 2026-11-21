/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 non-secure image (#590): it calls the secure world through the
 * tfw_s_version veneer and prints what it returns, then reads the first word
 * of Secure SRAM, which the SAU makes a SecureFault the secure handler
 * reports (test/integration/cortex-m/qemu_tz_test.sh reads both lines).
 */
#include <stdint.h>
#include "tfw/tz_s_api.h"
#include "tz_console.h"

extern uint32_t __stack;
extern uint32_t __data_load, __data_start, __data_end, __bss_start, __bss_end;

void tz_ns_reset(void);
void tz_ns_fault(void);

void tz_ns_reset(void)
{
    const uint32_t *src = &__data_load;
    uint32_t *dst;
    uint32_t v;

    for (dst = &__data_start; (uintptr_t)dst < (uintptr_t)&__data_end; )
        *dst++ = *src++;
    for (dst = &__bss_start; (uintptr_t)dst < (uintptr_t)&__bss_end; )
        *dst++ = 0u;

    tz_puts("tz: ns: up\r\n");
    v = tfw_s_version();
    tz_puts("tz: ns: tfw_s_version=");
    tz_puthex(v);
    tz_puts("\r\n");

    tz_puts("tz: ns: reading secure SRAM\r\n");
    v = *(volatile const uint32_t *)(uintptr_t)TZ_S_RAM_BASE;
    tz_puts("tz: ns: secure SRAM read returned ");
    tz_puthex(v);
    tz_puts("\r\n");
    for (;;)
        __asm volatile("wfi");
}

void tz_ns_fault(void)
{
    tz_puts("tz: ns: fault\r\n");
    for (;;)
        __asm volatile("wfi");
}

__attribute__((section(".vectors"), used))
static void (*const tz_ns_vectors[16])(void) = {
    (void (*)(void))(uintptr_t)&__stack,
    tz_ns_reset,
    tz_ns_fault, tz_ns_fault, tz_ns_fault, tz_ns_fault, tz_ns_fault,
    0, 0, 0, 0,
    tz_ns_fault, tz_ns_fault, 0, tz_ns_fault, tz_ns_fault,
};
