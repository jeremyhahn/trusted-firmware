/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TZ=1 secure image's start-up (#590, docs/plans/590.md): the SoC's own
 * opening (tz_soc_open, mps2's MPC and PPC), the SAU from tz_sau_regions,
 * SecureFault enabled, then the Non-secure world's VTOR and MSP and a BLXNS
 * to its reset handler. Faults stay Secure (AIRCR.BFHFNMINS is left 0), so a
 * Non-secure access to Secure memory lands in tz_s_securefault here.
 */
#include <arm_cmse.h>
#include <stdint.h>
#include "tz_console.h"
#include "tz_regions.h"
#include "tz_s.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define SCB_SHCSR    REG(0xE000ED24u)
#define SAU_CTRL     REG(0xE000EDD0u)
#define SAU_RNR      REG(0xE000EDD8u)
#define SAU_RBAR     REG(0xE000EDDCu)
#define SAU_RLAR     REG(0xE000EDE0u)
#define SAU_SFSR     REG(0xE000EDE4u)
#define SAU_SFAR     REG(0xE000EDE8u)
#define SCB_NS_VTOR  REG(0xE002ED08u)

#define SHCSR_FAULTS 0x000F0000u   /* MemManage, Bus, Usage, SecureFault enable */

extern uint32_t __stack;
extern uint32_t __data_load, __data_start, __data_end, __bss_start, __bss_end;

typedef void __attribute__((cmse_nonsecure_call)) tz_ns_entry_fn(void);

void tz_s_reset(void);
void tz_s_securefault(void);
void tz_s_fault(void);

/* A SoC with nothing to open before the SAU: the default. */
__attribute__((weak)) void tz_soc_open(void)
{
}

static void tz_halt(void)
{
    for (;;)
        __asm volatile("wfi");
}

void tz_s_sau_program(const struct tz_sau_region *r, size_t n)
{
    size_t i;

    SAU_CTRL = 0u;
    for (i = 0; i < n; i++) {
        SAU_RNR  = (uint32_t)i;
        SAU_RBAR = r[i].base & ~(TZ_SAU_GRANULE - 1u);
        SAU_RLAR = (r[i].limit & ~(TZ_SAU_GRANULE - 1u)) | ((uint32_t)r[i].nsc << 1) | 1u;
    }
    SAU_CTRL = 1u;   /* ENABLE; ALLNS 0: what no region names is Secure */
    __asm volatile("dsb\n\tisb" ::: "memory");
}

void tz_s_reset(void)
{
    const uint32_t *src = &__data_load;
    uint32_t *dst;
    const volatile uint32_t *ns_vec = (const volatile uint32_t *)(uintptr_t)TZ_NS_CODE_BASE;
    tz_ns_entry_fn *ns_reset;

    for (dst = &__data_start; (uintptr_t)dst < (uintptr_t)&__data_end; )
        *dst++ = *src++;
    for (dst = &__bss_start; (uintptr_t)dst < (uintptr_t)&__bss_end; )
        *dst++ = 0u;

    tz_soc_open();
    tz_s_sau_program(tz_sau_regions, tz_sau_region_count);
    SCB_SHCSR |= SHCSR_FAULTS;

    tz_console_init();
    tz_puts("tz: s: sau on, entering ns\r\n");

    SCB_NS_VTOR = TZ_NS_CODE_BASE;
    __asm volatile("msr msp_ns, %0" :: "r"(ns_vec[0]));
    ns_reset = (tz_ns_entry_fn *)cmse_nsfptr_create((tz_ns_entry_fn *)(uintptr_t)ns_vec[1]);
    ns_reset();

    tz_puts("tz: s: ns returned\r\n");
    tz_halt();
}

void tz_s_securefault(void)
{
    uint32_t sfsr = SAU_SFSR;
    uint32_t sfar = SAU_SFAR;

    tz_puts("tz: s: SecureFault SFSR=");
    tz_puthex(sfsr);
    tz_puts(" SFAR=");
    tz_puthex(sfar);
    tz_puts("\r\n");
    SAU_SFSR = sfsr;   /* write-one-to-clear */
    tz_halt();
}

void tz_s_fault(void)
{
    tz_puts("tz: s: fault\r\n");
    tz_halt();
}

/* The Secure vector table, at TZ_S_CODE_BASE (INITSVTOR on mps2, the
 * Secure boot address on the H563). */
__attribute__((section(".vectors"), used))
static void (*const tz_s_vectors[16])(void) = {
    (void (*)(void))(uintptr_t)&__stack,
    tz_s_reset,
    tz_s_fault,          /* NMI */
    tz_s_fault,          /* HardFault */
    tz_s_fault,          /* MemManage */
    tz_s_fault,          /* BusFault */
    tz_s_fault,          /* UsageFault */
    tz_s_securefault,    /* SecureFault */
    0, 0, 0,
    tz_s_fault,          /* SVCall */
    tz_s_fault,          /* DebugMonitor */
    0,
    tz_s_fault,          /* PendSV */
    tz_s_fault,          /* SysTick */
};
