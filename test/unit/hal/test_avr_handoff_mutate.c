/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_handoff.c (#163): the application's handle on the block
 * tfw-boot writes. On the device mk/avr.mk pins the section `.handoff` to the
 * address the bootloader also derives, so what the unit owns is that the
 * block IS that section: the whole of it, from its first byte, never NULL and
 * never a fresh copy.
 *
 * The host links `.handoff` as an orphan section of its own, so the suite
 * reads its own ELF section headers (/proc/self/exe) and checks the pointer
 * against the section's address and size. A block that lost its section
 * attribute lands in .bss and fails here exactly as it would land at the
 * wrong SRAM address on the part.
 */
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>

#include "unity.h"

#include "tfw/boot.h"
#include "tfw/hal.h"

static uintptr_t section_addr;
static uint64_t  section_size;
static int       section_found;

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

static void find_handoff_section(void)
{
    static Elf64_Shdr sh[256];
    static char       names[65536];
    Elf64_Ehdr        eh;
    FILE             *f = fopen("/proc/self/exe", "rb");
    unsigned          i;

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
        if (strcmp(&names[sh[i].sh_name], ".handoff") == 0) {
            section_addr  = (uintptr_t)sh[i].sh_addr + load_bias();
            section_size  = sh[i].sh_size;
            section_found = 1;
        }
    }
}

void setUp(void) {}
void tearDown(void) {}

static void test_block_is_the_whole_handoff_section(void)
{
    find_handoff_section();
    TEST_ASSERT_TRUE_MESSAGE(section_found, "no .handoff section in the image");
    TEST_ASSERT_EQUAL_UINT64(TFW_BOOT_HANDOFF_LEN, section_size);
    TEST_ASSERT_EQUAL_PTR((void *)section_addr, tfw_hal_handoff_block());
}

static void test_block_is_one_block_across_calls(void)
{
    uint8_t *a = tfw_hal_handoff_block();
    unsigned i;

    TEST_ASSERT_NOT_NULL(a);
    for (i = 0u; i < TFW_BOOT_HANDOFF_LEN; i++) {
        a[i] = (uint8_t)(0xA5u ^ i);
    }
    TEST_ASSERT_EQUAL_PTR(a, tfw_hal_handoff_block());
    for (i = 0u; i < TFW_BOOT_HANDOFF_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA5u ^ i), tfw_hal_handoff_block()[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_block_is_the_whole_handoff_section);
    RUN_TEST(test_block_is_one_block_across_calls);
    return UNITY_END();
}
