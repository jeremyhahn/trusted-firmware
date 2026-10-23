/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_early.c, register-level (#163): .init3 latches MCUSR, then
 * clears it, then disarms the watchdog -- in that order, since on the part
 * WDRF holds WDE set and the watchdog cannot be disarmed while it is -- and
 * the latch, not the cleared register, is what the reset cause decodes.
 *
 * The build defines `naked` away: on the host a naked function has no return,
 * and the suite calls it. wdt_disable is a call into this suite, which checks
 * the register and the latch at the moment it is made. Where each object
 * lands (.init3 for the code, .noinit for the latch) the suite reads from its
 * own ELF section headers.
 */
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>

#include "unity.h"

#include "avr/io.h"
#include "hal_early.h"

void tfw_hal_early_init(void);

static uint8_t  regs[TFW_AVR_REGS];
static unsigned disables;
static uint8_t  mcusr_at_disable, latch_at_disable;

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(TFW_AVR_MCUSR, id, "hal_early.c touched a register other than TFW_AVR_MCUSR");
    return &regs[id];
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("hal_early.c reached a register by address");
    return 0;
}

volatile uint16_t *tfw_avr_mem16(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("hal_early.c made a word access");
    return 0;
}

void wdt_disable(void)
{
    disables++;
    mcusr_at_disable = regs[TFW_AVR_MCUSR];
    latch_at_disable = tfw_hal_early_flags;
}

/* The load bias: where PT_PHDR landed minus where the file says it is. */
static uintptr_t load_bias(void)
{
    const Elf64_Phdr *ph = (const Elf64_Phdr *)getauxval(AT_PHDR);
    unsigned long     n  = getauxval(AT_PHNUM);
    unsigned long     i;

    for (i = 0u; i < n; i++) {
        if (ph[i].p_type == PT_PHDR) {
            return (uintptr_t)ph - (uintptr_t)ph[i].p_vaddr;
        }
    }
    return 0u;    /* not PIE: no PT_PHDR is needed, and no bias */
}

/* Whether [p, p + len) lies inside the section `name` of this image. */
static int in_section(const char *name, uintptr_t p, size_t len)
{
    static Elf64_Shdr sh[256];
    static char       names[65536];
    Elf64_Ehdr        eh;
    FILE             *f = fopen("/proc/self/exe", "rb");
    unsigned          i;
    int               inside = 0;

    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot open /proc/self/exe");
    TEST_ASSERT_EQUAL_size_t(1u, fread(&eh, sizeof eh, 1u, f));
    TEST_ASSERT_EQUAL_MEMORY(ELFMAG, eh.e_ident, SELFMAG);
    TEST_ASSERT_EQUAL_UINT8(ELFCLASS64, eh.e_ident[EI_CLASS]);
    TEST_ASSERT_TRUE(eh.e_shnum <= 256u);
    TEST_ASSERT_EQUAL_INT(0, fseek(f, (long)eh.e_shoff, SEEK_SET));
    TEST_ASSERT_EQUAL_size_t(eh.e_shnum, fread(sh, sizeof sh[0], eh.e_shnum, f));
    TEST_ASSERT_TRUE(sh[eh.e_shstrndx].sh_size <= sizeof names);
    TEST_ASSERT_EQUAL_INT(0, fseek(f, (long)sh[eh.e_shstrndx].sh_offset, SEEK_SET));
    TEST_ASSERT_EQUAL_size_t(1u, fread(names, sh[eh.e_shstrndx].sh_size, 1u, f));
    fclose(f);

    for (i = 0u; i < eh.e_shnum; i++) {
        uintptr_t lo = (uintptr_t)sh[i].sh_addr + load_bias();

        if (strcmp(&names[sh[i].sh_name], name) == 0 &&
            p >= lo && p + len <= lo + sh[i].sh_size) {
            inside = 1;
        }
    }
    return inside;
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    disables            = 0u;
    mcusr_at_disable    = 0xEEu;
    latch_at_disable    = 0xEEu;
    tfw_hal_early_flags = 0xEEu;
}

void tearDown(void) {}

static void test_init_latches_then_clears_then_disarms(void)
{
    regs[TFW_AVR_MCUSR] = 0x0Bu;              /* WDRF | EXTRF | PORF, accumulated */
    tfw_hal_early_init();
    TEST_ASSERT_EQUAL_HEX8(0x0Bu, tfw_hal_early_flags);
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_MCUSR]);
    TEST_ASSERT_EQUAL_UINT(1u, disables);
    TEST_ASSERT_EQUAL_HEX8(0x0Bu, latch_at_disable);    /* latched before */
    TEST_ASSERT_EQUAL_HEX8(0x00u, mcusr_at_disable);    /* WDRF cleared before */
}

static void test_init_latches_every_bit(void)
{
    regs[TFW_AVR_MCUSR] = 0xFFu;
    tfw_hal_early_init();
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_hal_early_flags);
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_MCUSR]);
    regs[TFW_AVR_MCUSR] = 0x00u;
    tfw_hal_early_init();
    TEST_ASSERT_EQUAL_HEX8(0x00u, tfw_hal_early_flags);
    TEST_ASSERT_EQUAL_UINT(2u, disables);
}

static void test_reset_cause_decodes_the_latch(void)
{
    static const struct { uint8_t mcusr; tfw_reset_cause_t cause; } c[] = {
        { 0x00u, TFW_RESET_UNKNOWN },  { 0x01u, TFW_RESET_POWER_ON },
        { 0x02u, TFW_RESET_EXTERNAL }, { 0x04u, TFW_RESET_BROWNOUT },
        { 0x08u, TFW_RESET_WATCHDOG }, { 0x09u, TFW_RESET_POWER_ON },
        { 0x0Fu, TFW_RESET_BROWNOUT },
    };
    unsigned i;

    for (i = 0u; i < sizeof c / sizeof c[0]; i++) {
        regs[TFW_AVR_MCUSR] = c[i].mcusr;
        tfw_hal_early_init();
        TEST_ASSERT_EQUAL_INT(c[i].cause, tfw_hal_early_reset_cause());
    }
}

static void test_reset_cause_reads_the_latch_not_the_register(void)
{
    tfw_hal_early_flags = 0x08u;
    regs[TFW_AVR_MCUSR] = 0x04u;
    TEST_ASSERT_EQUAL_INT(TFW_RESET_WATCHDOG, tfw_hal_early_reset_cause());
    TEST_ASSERT_EQUAL_HEX8(0x04u, regs[TFW_AVR_MCUSR]);
    TEST_ASSERT_EQUAL_UINT(0u, disables);
}

static void test_code_in_init3_and_latch_in_noinit(void)
{
    TEST_ASSERT_TRUE_MESSAGE(in_section(".init3", (uintptr_t)&tfw_hal_early_init, 1u),
                             "tfw_hal_early_init is not in .init3");
    TEST_ASSERT_TRUE_MESSAGE(in_section(".noinit", (uintptr_t)&tfw_hal_early_flags,
                                        sizeof tfw_hal_early_flags),
                             "tfw_hal_early_flags is not in .noinit");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_latches_then_clears_then_disarms);
    RUN_TEST(test_init_latches_every_bit);
    RUN_TEST(test_reset_cause_decodes_the_latch);
    RUN_TEST(test_reset_cause_reads_the_latch_not_the_register);
    RUN_TEST(test_code_in_init3_and_latch_in_noinit);
    return UNITY_END();
}
