/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE EARLIEST CODE THAT RUNS -- in BOTH images, as src/hal/avr/hal_early.c
 * is on AVR and for its reason: the sequence whose failure is a part that
 * resets forever exists once.
 *
 * There is no crt0. mk/arch/cortex-m.mk links with -nostartfiles, and this
 * file is the vector table and the reset handler, in C: the core loads the
 * stack pointer from word 0 and enters word 1 with nothing else set up, which
 * is enough for a C function that touches no global until it has made them.
 *
 * The reset handler, in order:
 *
 *   1. reads and clears the reset marker (hal_early.h), before anything can
 *      write it;
 *   2. disarms the watchdog (the SoC's tfw_cm_soc_early): an armed watchdog
 *      that survived the reset would bite the image that has only just
 *      started, and tfw-boot runs this after every one of its bites;
 *   3. points VTOR at this image's table, so an image entered by tfw-boot's
 *      jump takes its own interrupts and not tfw-boot's;
 *   4. copies .data and zeroes .bss (mk/arch/cortex-m/<soc>.ld's symbols);
 *   5. calls main.
 */

#include <stdint.h>

#include "tfw/types.h"
#include "cm_regs.h"
#include "hal_early.h"
#include "hal_fault_frame.h"

extern uint32_t __stack, __tfw_ram_base;
extern uint32_t __data_load_start, __data_start, __data_end;
extern uint32_t __bss_start, __bss_end;

int main(void);
_Noreturn void tfw_cm_reset(void);
_Noreturn void tfw_cm_fault_save(uintptr_t sp);
static _Noreturn void reset_marked(uint32_t mark);

/* Pinned to __tfw_resetmark by the linker script, and in a NOLOAD section of
 * its own, so neither image's .bss clear nor its .data copy touches it. */
volatile uint32_t tfw_cm_reset_mark __attribute__((section(".resetmark")));

/* The fault record (hal_fault_frame.h), pinned to __tfw_faultframe the same
 * way. The reset handler leaves it alone: it outlives the fault's reset for
 * a debugger to read. */
volatile uint32_t tfw_cm_fault_frame[TFW_CM_FAULT_WORDS]
    __attribute__((section(".faultframe")));

static tfw_reset_cause_t latched;

/* An exception or interrupt nothing handles parks the core here; a fault
 * does not (tfw_cm_fault). A watchdog, if one is armed, then resets it, and
 * the marker says so: the NMI is the watchdog's first expiry, and parking in
 * it leaves the second expiry to reset the core with the watchdog's marker. */
void tfw_cm_unhandled(void);
void tfw_cm_unhandled(void)
{
    for (;;) { }
}

/* Weak, so the file that owns a source defines the handler by name and the
 * table needs no knowledge of which files are linked: SysTick_Handler is
 * hal_tick.c's in the application and absent from tfw-boot, which polls.
 * tfw_cm_irq<n> is external interrupt n, defined by the SoC's file that drives
 * it (src/hal/cortex-m/mps2/hal_uart.c defines the UART's). */
#define TFW_CM_WEAK(name) void name(void) __attribute__((weak, alias("tfw_cm_unhandled")));

TFW_CM_WEAK(SysTick_Handler)
TFW_CM_WEAK(tfw_cm_irq0)  TFW_CM_WEAK(tfw_cm_irq1)  TFW_CM_WEAK(tfw_cm_irq2)
TFW_CM_WEAK(tfw_cm_irq3)  TFW_CM_WEAK(tfw_cm_irq4)  TFW_CM_WEAK(tfw_cm_irq5)
TFW_CM_WEAK(tfw_cm_irq6)  TFW_CM_WEAK(tfw_cm_irq7)  TFW_CM_WEAK(tfw_cm_irq8)
TFW_CM_WEAK(tfw_cm_irq9)  TFW_CM_WEAK(tfw_cm_irq10) TFW_CM_WEAK(tfw_cm_irq11)
TFW_CM_WEAK(tfw_cm_irq12) TFW_CM_WEAK(tfw_cm_irq13) TFW_CM_WEAK(tfw_cm_irq14)
TFW_CM_WEAK(tfw_cm_irq15) TFW_CM_WEAK(tfw_cm_irq16) TFW_CM_WEAK(tfw_cm_irq17)
TFW_CM_WEAK(tfw_cm_irq18) TFW_CM_WEAK(tfw_cm_irq19) TFW_CM_WEAK(tfw_cm_irq20)
TFW_CM_WEAK(tfw_cm_irq21) TFW_CM_WEAK(tfw_cm_irq22) TFW_CM_WEAK(tfw_cm_irq23)
TFW_CM_WEAK(tfw_cm_irq24) TFW_CM_WEAK(tfw_cm_irq25) TFW_CM_WEAK(tfw_cm_irq26)
TFW_CM_WEAK(tfw_cm_irq27) TFW_CM_WEAK(tfw_cm_irq28) TFW_CM_WEAK(tfw_cm_irq29)
TFW_CM_WEAK(tfw_cm_irq30) TFW_CM_WEAK(tfw_cm_irq31) TFW_CM_WEAK(tfw_cm_irq32)
TFW_CM_WEAK(tfw_cm_irq33) TFW_CM_WEAK(tfw_cm_irq34) TFW_CM_WEAK(tfw_cm_irq35)
TFW_CM_WEAK(tfw_cm_irq36) TFW_CM_WEAK(tfw_cm_irq37) TFW_CM_WEAK(tfw_cm_irq38)
TFW_CM_WEAK(tfw_cm_irq39) TFW_CM_WEAK(tfw_cm_irq40) TFW_CM_WEAK(tfw_cm_irq41)
TFW_CM_WEAK(tfw_cm_irq42) TFW_CM_WEAK(tfw_cm_irq43) TFW_CM_WEAK(tfw_cm_irq44)
TFW_CM_WEAK(tfw_cm_irq45) TFW_CM_WEAK(tfw_cm_irq46) TFW_CM_WEAK(tfw_cm_irq47)
TFW_CM_WEAK(tfw_cm_irq48) TFW_CM_WEAK(tfw_cm_irq49) TFW_CM_WEAK(tfw_cm_irq50)
TFW_CM_WEAK(tfw_cm_irq51) TFW_CM_WEAK(tfw_cm_irq52) TFW_CM_WEAK(tfw_cm_irq53)
TFW_CM_WEAK(tfw_cm_irq54) TFW_CM_WEAK(tfw_cm_irq55) TFW_CM_WEAK(tfw_cm_irq56)
TFW_CM_WEAK(tfw_cm_irq57) TFW_CM_WEAK(tfw_cm_irq58) TFW_CM_WEAK(tfw_cm_irq59)
TFW_CM_WEAK(tfw_cm_irq60) TFW_CM_WEAK(tfw_cm_irq61) TFW_CM_WEAK(tfw_cm_irq62)
TFW_CM_WEAK(tfw_cm_irq63) TFW_CM_WEAK(tfw_cm_irq64) TFW_CM_WEAK(tfw_cm_irq65)
TFW_CM_WEAK(tfw_cm_irq66) TFW_CM_WEAK(tfw_cm_irq67) TFW_CM_WEAK(tfw_cm_irq68)
TFW_CM_WEAK(tfw_cm_irq69) TFW_CM_WEAK(tfw_cm_irq70) TFW_CM_WEAK(tfw_cm_irq71)
TFW_CM_WEAK(tfw_cm_irq72) TFW_CM_WEAK(tfw_cm_irq73) TFW_CM_WEAK(tfw_cm_irq74)
TFW_CM_WEAK(tfw_cm_irq75) TFW_CM_WEAK(tfw_cm_irq76) TFW_CM_WEAK(tfw_cm_irq77)
TFW_CM_WEAK(tfw_cm_irq78) TFW_CM_WEAK(tfw_cm_irq79)

/* Sixteen core exceptions and the first 80 external interrupts: every source
 * a SoC this port supports uses (the AN505's UART0 is 32 and 33, the AN500's 0
 * and 1, the i.MX 95 M7's LPUART3 64). 96 words, so the table's 512-byte
 * alignment for VTOR is the region base's. */
#define TFW_CM_IRQ_COUNT 80u
typedef void (*tfw_cm_vector_t)(void);

const tfw_cm_vector_t tfw_cm_vectors[16u + TFW_CM_IRQ_COUNT]
    __attribute__((section(".vectors"), used)) = {
    (tfw_cm_vector_t)(uintptr_t)&__stack,   /* 0: the initial stack pointer */
    tfw_cm_reset,                           /* 1: reset */
    tfw_cm_unhandled,                       /* 2: NMI (the watchdog's first expiry) */
    tfw_cm_fault,                           /* 3: HardFault */
    tfw_cm_fault,                           /* 4: MemManage */
    tfw_cm_fault,                           /* 5: BusFault */
    tfw_cm_fault,                           /* 6: UsageFault */
    tfw_cm_unhandled,                       /* 7: SecureFault (ARMv8-M) */
    0, 0, 0,                                /* 8-10: reserved */
    tfw_cm_unhandled,                       /* 11: SVCall */
    tfw_cm_unhandled,                       /* 12: DebugMonitor */
    0,                                      /* 13: reserved */
    tfw_cm_unhandled,                       /* 14: PendSV */
    SysTick_Handler,                        /* 15 */
    tfw_cm_irq0,  tfw_cm_irq1,  tfw_cm_irq2,  tfw_cm_irq3,
    tfw_cm_irq4,  tfw_cm_irq5,  tfw_cm_irq6,  tfw_cm_irq7,
    tfw_cm_irq8,  tfw_cm_irq9,  tfw_cm_irq10, tfw_cm_irq11,
    tfw_cm_irq12, tfw_cm_irq13, tfw_cm_irq14, tfw_cm_irq15,
    tfw_cm_irq16, tfw_cm_irq17, tfw_cm_irq18, tfw_cm_irq19,
    tfw_cm_irq20, tfw_cm_irq21, tfw_cm_irq22, tfw_cm_irq23,
    tfw_cm_irq24, tfw_cm_irq25, tfw_cm_irq26, tfw_cm_irq27,
    tfw_cm_irq28, tfw_cm_irq29, tfw_cm_irq30, tfw_cm_irq31,
    tfw_cm_irq32, tfw_cm_irq33, tfw_cm_irq34, tfw_cm_irq35,
    tfw_cm_irq36, tfw_cm_irq37, tfw_cm_irq38, tfw_cm_irq39,
    tfw_cm_irq40, tfw_cm_irq41, tfw_cm_irq42, tfw_cm_irq43,
    tfw_cm_irq44, tfw_cm_irq45, tfw_cm_irq46, tfw_cm_irq47,
    tfw_cm_irq48, tfw_cm_irq49, tfw_cm_irq50, tfw_cm_irq51,
    tfw_cm_irq52, tfw_cm_irq53, tfw_cm_irq54, tfw_cm_irq55,
    tfw_cm_irq56, tfw_cm_irq57, tfw_cm_irq58, tfw_cm_irq59,
    tfw_cm_irq60, tfw_cm_irq61, tfw_cm_irq62, tfw_cm_irq63,
    tfw_cm_irq64, tfw_cm_irq65, tfw_cm_irq66, tfw_cm_irq67,
    tfw_cm_irq68, tfw_cm_irq69, tfw_cm_irq70, tfw_cm_irq71,
    tfw_cm_irq72, tfw_cm_irq73, tfw_cm_irq74, tfw_cm_irq75,
    tfw_cm_irq76, tfw_cm_irq77, tfw_cm_irq78, tfw_cm_irq79,
};

_Noreturn void tfw_cm_reset(void)
{
    uint32_t  mark = tfw_cm_reset_mark;
    uint32_t       *dst;
    const uint32_t *src;

    tfw_cm_reset_mark = 0u;
    tfw_cm_soc_early();
    TFW_CM_VTOR = (uint32_t)(uintptr_t)tfw_cm_vectors;
    tfw_cm_dsb();
    tfw_cm_isb();

    /* The linker symbols are distinct objects to C, so their addresses are
     * compared as integers: relational operators on pointers into different
     * objects are undefined (C11 6.5.8p5). */
    for (src = &__data_load_start, dst = &__data_start;
         (uintptr_t)dst < (uintptr_t)&__data_end; ) {
        *dst++ = *src++;
    }
    for (dst = &__bss_start; (uintptr_t)dst < (uintptr_t)&__bss_end; ) {
        *dst++ = 0u;
    }

    latched = tfw_cm_reset_decode(mark);
    (void)main();
    for (;;) { }
}

tfw_reset_cause_t tfw_hal_early_reset_cause(void)
{
    return latched;
}

_Noreturn void tfw_cm_system_reset(void)
{
    reset_marked(TFW_CM_MARK_RESETREQ);
}

/*/* A fault is the image's fatal error. MemManage, BusFault and UsageFault
 * escalate to HardFault while SHCSR leaves them disabled, as both images do,
 * and their vectors come here too for the image that enables one. The core
 * is reset at once with the panic marker, rather than parked: SYS_INFO then
 * says TFW_RESET_PANIC, and a trial image that faults ends its trial at the
 * reset instead of at the watchdog's bite. A reset from handler mode is
 * allowed: AIRCR takes a privileged write at any priority (ARMv7-M ARM DDI
 * 0403E, B3.2.6).
 *
 * Naked, so the stack pointer it hands on is the one the core stacked the
 * faulting frame on, untouched by a prologue: EXC_RETURN in LR says which,
 * bit 2 (SPSEL) clear for the main stack and set for the process stack
 * (DDI 0403E, B1.5.8). Basic asm only, as GCC requires in a naked function. */
__attribute__((naked)) void tfw_cm_fault(void)
{
    __asm__ volatile (
        "tst lr, #4\n\t"
        "ite eq\n\t"
        "mrseq r0, msp\n\t"
        "mrsne r0, psp\n\t"
        "b tfw_cm_fault_save\n\t");
}

/* Keeps the faulting frame's PC, LR and xPSR in the record (hal_fault_frame.h)
 * and resets with the panic marker. Named only from tfw_cm_fault's asm, which
 * LTO cannot see: used and externally_visible keep it, and its name. */
__attribute__((used, externally_visible)) _Noreturn void tfw_cm_fault_save(uintptr_t sp)
{
    tfw_cm_fault_keep(tfw_cm_fault_frame, sp, (uintptr_t)&__tfw_ram_base,
                      (uintptr_t)&__stack);
    reset_marked(TFW_CM_MARK_PANIC);
}

static _Noreturn void reset_marked(uint32_t mark)
{
    tfw_cm_irq_off();
    tfw_cm_reset_mark = mark;
    tfw_cm_dsb();
    TFW_CM_AIRCR = TFW_CM_AIRCR_VECTKEY | TFW_CM_AIRCR_SYSRESETREQ;
    tfw_cm_dsb();
    for (;;) { }
}
