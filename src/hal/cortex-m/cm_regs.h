/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The core's own registers: the System Control Block, SysTick and the NVIC,
 * at the addresses the architecture fixes for every Cortex-M (ARMv7-M and
 * ARMv8-M ARM, the System Control Space). Nothing SoC-specific is here; a
 * part's peripherals are in src/hal/cortex-m/<soc>/.
 *
 * Included by both images: the application's HAL and tfw-boot's
 * (boot/hal/cortex-m/, through -I$(HAL_DIR)), as src/hal/avr/avr_usart_regs.h
 * is on AVR.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

/* A host suite may define its own, pointing the registers at a model. */
#ifndef TFW_CM_REG
#define TFW_CM_REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#endif

/* System Control Block. */
#define TFW_CM_VTOR  TFW_CM_REG(0xE000ED08u)
#define TFW_CM_AIRCR TFW_CM_REG(0xE000ED0Cu)
#define TFW_CM_AIRCR_VECTKEY     0x05FA0000u
#define TFW_CM_AIRCR_SYSRESETREQ 0x00000004u

/* SysTick, counting the processor clock (CLKSOURCE 1). */
#define TFW_CM_SYST_CSR TFW_CM_REG(0xE000E010u)
#define TFW_CM_SYST_RVR TFW_CM_REG(0xE000E014u)
#define TFW_CM_SYST_CVR TFW_CM_REG(0xE000E018u)
#define TFW_CM_SYST_CSR_ENABLE    0x1u
#define TFW_CM_SYST_CSR_TICKINT   0x2u
#define TFW_CM_SYST_CSR_CLKSOURCE 0x4u
#define TFW_CM_SYST_RVR_MAX       0x00FFFFFFu

/* NVIC: one bit per external interrupt, thirty-two to a register. */
#define TFW_CM_NVIC_ISER(n) TFW_CM_REG(0xE000E100u + 4u * (n))
#define TFW_CM_NVIC_ICER(n) TFW_CM_REG(0xE000E180u + 4u * (n))
#define TFW_CM_NVIC_ICPR(n) TFW_CM_REG(0xE000E280u + 4u * (n))

static inline void tfw_cm_nvic_enable(uint32_t irq)
{
    TFW_CM_NVIC_ICPR(irq / 32u) = 1u << (irq % 32u);
    TFW_CM_NVIC_ISER(irq / 32u) = 1u << (irq % 32u);
}

static inline void tfw_cm_nvic_disable(uint32_t irq)
{
    TFW_CM_NVIC_ICER(irq / 32u) = 1u << (irq % 32u);
    TFW_CM_NVIC_ICPR(irq / 32u) = 1u << (irq % 32u);
}

static inline void tfw_cm_dsb(void) { __asm__ volatile ("dsb 0xF" ::: "memory"); }
static inline void tfw_cm_isb(void) { __asm__ volatile ("isb 0xF" ::: "memory"); }

static inline void tfw_cm_irq_on(void)  { __asm__ volatile ("cpsie i" ::: "memory"); }
static inline void tfw_cm_irq_off(void) { __asm__ volatile ("cpsid i" ::: "memory"); }

/* PRIMASK saved and interrupts masked; restored by tfw_cm_irq_restore. The
 * one critical section the UART's transmit start needs. */
static inline uint32_t tfw_cm_irq_save(void)
{
    uint32_t primask;

    __asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r" (primask) :: "memory");
    return primask;
}

static inline void tfw_cm_irq_restore(uint32_t primask)
{
    __asm__ volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

/* A function that runs from SRAM while the flash it would otherwise run from
 * is unreadable: in its own .ramfunc section, which the SoC's linker script
 * copies with .data; noipa, so no inlining or cloning moves a byte of it back
 * into flash; long_call, since SRAM is out of a BL's reach of the flash that
 * calls it. */
#define TFW_CM_RAMFUNC(name) __attribute__((noipa, long_call, section(".ramfunc." name)))

#endif /* TFW_CM_REGS_H */
