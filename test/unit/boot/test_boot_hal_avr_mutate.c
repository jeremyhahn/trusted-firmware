/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/hal/avr/boot_hal_avr.c, register-level (#163): the bootloader's flash
 * read and page write, the EEPROM store, the polled UART, the handoff block,
 * the jump to the application, the watchdog reset and arm, and the reset
 * cause.
 *
 * Built twice, as the 328P (near flash reads, 128-byte pages, no EIND) and as
 * the 2560 (far reads, 256-byte pages, EIND); FLASHEND, SPM_PAGESIZE and EIND
 * come from the build's -D, as avr-libc brings them from the part's header.
 * The SPM steps, the flash and EEPROM reads and cli() are calls into this
 * suite, which logs their order. The jump calls address zero: the suite takes
 * the fault and checks it was a call to zero. tfw_boot_reset ends in an empty
 * spin; a CPU-time timer, which runs only while the unit spins, takes the
 * suite out of it.
 */
#define _GNU_SOURCE                   /* sigaction, sigsetjmp, getauxval */
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include <elf.h>
#include <stdio.h>
#include <sys/auxv.h>

#include "unity.h"

#include "avr/io.h"
#include "avr/pgmspace.h"
#include "tfw/boot.h"
#include "boot_hal.h"
#include "hal_early.h"

#define PAGE ((uint32_t)SPM_PAGESIZE)

static const int far_flash = (FLASHEND > 0xFFFFUL);
static const int has_eind  = (FLASHEND > 0x1FFFFUL);

static uint8_t  regs[TFW_AVR_REGS];
static char     ev[1024];
static unsigned nev;
static uint8_t  wdt_seen[4];
static unsigned nwdt;
static unsigned ucsra_reads, udre_in;

static uint32_t fill_addr[256];
static uint16_t fill_word[256];
static unsigned nfill;
static uint32_t erase_addr, write_addr;

static uint32_t read_addr[64];
static unsigned nread;

static uint8_t  ee[TFW_EEPROM_SIZE];
static uintptr_t ee_addr;
static size_t    ee_len;

/* jump and reset: how the suite gets back */
static sigjmp_buf          back;
static volatile int        fault_sig, ticks;
static volatile uintptr_t  fault_addr;

const char *__asan_default_options(void);
const char *__asan_default_options(void)
{
    return "handle_segv=0";      /* the suite takes the jump's fault itself */
}

static void log_ev(char c)
{
    TEST_ASSERT_TRUE(nev < sizeof ev - 1u);
    ev[nev++] = c;
}

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    TEST_ASSERT_TRUE_MESSAGE(id == TFW_AVR_UDR0 || id == TFW_AVR_UCSR0A || id == TFW_AVR_UCSR0B ||
                             id == TFW_AVR_UCSR0C || id == TFW_AVR_UBRR0H || id == TFW_AVR_UBRR0L ||
                             id == TFW_AVR_SREG || id == TFW_AVR_WDTCSR || id == TFW_AVR_EIND,
                             "boot_hal_avr.c touched a register outside its set");
    if (id == TFW_AVR_WDTCSR) {
        TEST_ASSERT_TRUE(nwdt < sizeof wdt_seen);
        wdt_seen[nwdt++] = regs[id];       /* the value the previous write left */
    }
    if (id == TFW_AVR_EIND) {
        log_ev('E');
    }
    if (id == TFW_AVR_UCSR0A) {
        ucsra_reads++;
        if (udre_in > 0u && --udre_in == 0u) {
            regs[id] = (uint8_t)(regs[id] | 0x20u);     /* the shift register drains */
        }
    }
    return &regs[id];
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("boot_hal_avr.c reached a register by address");
    return 0;
}

volatile uint16_t *tfw_avr_mem16(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("boot_hal_avr.c made a word access");
    return 0;
}

void cli(void)
{
    log_ev('c');
    regs[TFW_AVR_SREG] = (uint8_t)(regs[TFW_AVR_SREG] & 0x7Fu);
}

void sei(void) { log_ev('i'); }

static void spm_step(char c)
{
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0u, regs[TFW_AVR_SREG] & 0x80u, "SPM with interrupts on");
    log_ev(c);
}

void boot_page_erase(uint32_t addr) { spm_step('e'); erase_addr = addr; }
void boot_page_write(uint32_t addr) { spm_step('w'); write_addr = addr; }
void boot_spm_busy_wait(void)       { spm_step('b'); }
void boot_rww_enable(void)          { log_ev('r'); }

void boot_page_fill(uint32_t addr, uint16_t word)
{
    spm_step('f');
    TEST_ASSERT_TRUE(nfill < 256u);
    fill_addr[nfill] = addr;
    fill_word[nfill] = word;
    nfill++;
}

static uint8_t flash_at(uint32_t a)
{
    return (uint8_t)(a ^ (a >> 8) ^ (a >> 16) ^ 0x5Au);
}

uint8_t pgm_read_byte(uint16_t addr)
{
    log_ev('n');
    TEST_ASSERT_TRUE(nread < 64u);
    read_addr[nread++] = addr;
    return flash_at(addr);
}

uint8_t pgm_read_byte_far(uint_farptr_t addr)
{
    log_ev('F');
    TEST_ASSERT_TRUE(nread < 64u);
    read_addr[nread++] = addr;
    return flash_at(addr);
}

void eeprom_read_block(void *dst, const void *src, size_t n)
{
    log_ev('R');
    ee_addr = (uintptr_t)src;
    ee_len  = n;
    TEST_ASSERT_TRUE(ee_addr + n <= sizeof ee);
    memcpy(dst, &ee[ee_addr], n);
}

void eeprom_update_block(const void *src, void *dst, size_t n)
{
    log_ev('U');
    ee_addr = (uintptr_t)dst;
    ee_len  = n;
    TEST_ASSERT_TRUE(ee_addr + n <= sizeof ee);
    memcpy(&ee[ee_addr], src, n);
}

void eeprom_write_block(const void *src, void *dst, size_t n)
{
    (void)src; (void)dst; (void)n;
    TEST_FAIL_MESSAGE("the store programs every byte: eeprom_write_block, not update");
}

uint8_t eeprom_read_byte(const uint8_t *p)                { (void)p; TEST_FAIL(); return 0; }
void    eeprom_write_byte(uint8_t *p, uint8_t value)      { (void)p; (void)value; TEST_FAIL(); }
void    eeprom_update_byte(uint8_t *p, uint8_t value)     { (void)p; (void)value; TEST_FAIL(); }

tfw_reset_cause_t tfw_hal_early_reset_cause(void)
{
    log_ev('x');
    return TFW_RESET_BROWNOUT;
}

static void on_fault(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    fault_sig  = sig;
    fault_addr = (uintptr_t)si->si_addr;
    siglongjmp(back, 1);
}

static void on_tick(int sig)
{
    (void)sig;
    if (++ticks >= 2) {
        siglongjmp(back, 1);
    }
}

/* Whether [p, p + len) lies inside the section `name` of this image. */
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
    return 0u;
}

static uint64_t section_size(const char *name, uintptr_t *addr)
{
    static Elf64_Shdr sh[256];
    static char       names[65536];
    Elf64_Ehdr        eh;
    FILE             *f = fopen("/proc/self/exe", "rb");
    unsigned          i;
    uint64_t          size = 0u;

    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot open /proc/self/exe");
    TEST_ASSERT_EQUAL_size_t(1u, fread(&eh, sizeof eh, 1u, f));
    TEST_ASSERT_EQUAL_MEMORY(ELFMAG, eh.e_ident, SELFMAG);
    TEST_ASSERT_TRUE(eh.e_shnum <= 256u);
    TEST_ASSERT_EQUAL_INT(0, fseek(f, (long)eh.e_shoff, SEEK_SET));
    TEST_ASSERT_EQUAL_size_t(eh.e_shnum, fread(sh, sizeof sh[0], eh.e_shnum, f));
    TEST_ASSERT_TRUE(sh[eh.e_shstrndx].sh_size <= sizeof names);
    TEST_ASSERT_EQUAL_INT(0, fseek(f, (long)sh[eh.e_shstrndx].sh_offset, SEEK_SET));
    TEST_ASSERT_EQUAL_size_t(1u, fread(names, sh[eh.e_shstrndx].sh_size, 1u, f));
    fclose(f);
    for (i = 0u; i < eh.e_shnum; i++) {
        if (strcmp(&names[sh[i].sh_name], name) == 0) {
            *addr = (uintptr_t)sh[i].sh_addr + load_bias();
            size  = sh[i].sh_size;
        }
    }
    return size;
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    memset(ev, 0, sizeof ev);
    memset(ee, 0xFF, sizeof ee);
    memset(wdt_seen, 0, sizeof wdt_seen);
    nev = nwdt = nfill = nread = ucsra_reads = udre_in = 0u;
    erase_addr = write_addr = 0xDEADu;
    ee_addr = 0xDEADu;
    ee_len  = 0u;
    fault_sig = ticks = 0;
    fault_addr = 1u;
}

void tearDown(void) {}

/* ---------------------------------------------------------------- flash */

static void test_flash_read_takes_the_parts_read_at_each_address(void)
{
    uint8_t  buf[17];
    unsigned i;

    memset(buf, 0xEE, sizeof buf);
    tfw_boot_flash_read(0xFFF8u, buf, 16u);        /* across 64 KB */
    TEST_ASSERT_EQUAL_UINT(16u, nread);
    for (i = 0u; i < 16u; i++) {
        uint32_t want = far_flash ? 0xFFF8u + i : (uint16_t)(0xFFF8u + i);

        TEST_ASSERT_EQUAL_HEX32(want, read_addr[i]);
        TEST_ASSERT_EQUAL_HEX8(flash_at(want), buf[i]);
        TEST_ASSERT_EQUAL_CHAR(far_flash ? 'F' : 'n', ev[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[16]);        /* len bytes, no more */
}

static void test_flash_read_of_nothing_reads_nothing(void)
{
    uint8_t buf[1] = { 0xEEu };

    tfw_boot_flash_read(0x100u, buf, 0u);
    TEST_ASSERT_EQUAL_UINT(0u, nread);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[0]);
    tfw_boot_flash_read(0x100u, buf, 1u);
    TEST_ASSERT_EQUAL_UINT(1u, nread);
    TEST_ASSERT_EQUAL_HEX32(0x100u, read_addr[0]);
}

static void test_flash_page_size_is_the_parts(void)
{
    TEST_ASSERT_EQUAL_UINT16(SPM_PAGESIZE, tfw_boot_flash_page_size());
}

static void test_write_page_erases_fills_writes_in_order(void)
{
    static uint8_t data[256];
    char           want[300];
    uint32_t       addr = TFW_BOOT_APP_BASE + PAGE;
    unsigned       i;

    for (i = 0u; i < PAGE; i++) {
        data[i] = (uint8_t)(i * 3u + 1u);
    }
    regs[TFW_AVR_SREG] = 0x83u;                    /* I set, and C and Z */
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_flash_write_page(addr, data));

    memset(want, 0, sizeof want);
    want[0] = 'c'; want[1] = 'e'; want[2] = 'b';
    for (i = 0u; i < PAGE / 2u; i++) {
        want[3u + i] = 'f';
    }
    memcpy(&want[3u + PAGE / 2u], "wbr", 3u);
    TEST_ASSERT_EQUAL_STRING(want, ev);

    TEST_ASSERT_EQUAL_HEX32(addr, erase_addr);
    TEST_ASSERT_EQUAL_HEX32(addr, write_addr);
    TEST_ASSERT_EQUAL_UINT(PAGE / 2u, nfill);
    for (i = 0u; i < PAGE / 2u; i++) {
        TEST_ASSERT_EQUAL_HEX32(addr + 2u * i, fill_addr[i]);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)(data[2u * i] | (data[2u * i + 1u] << 8)), fill_word[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0x83u, regs[TFW_AVR_SREG]);    /* restored whole */
}

static void test_write_page_restores_a_clear_flag_clear(void)
{
    static uint8_t data[256];

    regs[TFW_AVR_SREG] = 0x02u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_flash_write_page(TFW_BOOT_APP_BASE, data));
    TEST_ASSERT_EQUAL_HEX8(0x02u, regs[TFW_AVR_SREG]);
    TEST_ASSERT_EQUAL_HEX32(TFW_BOOT_APP_BASE, erase_addr);
}

static void test_write_page_refuses_a_misaligned_address(void)
{
    static uint8_t data[256];
    uint32_t       off[] = { 1u, 2u, PAGE / 2u, PAGE - 1u };
    unsigned       i;

    for (i = 0u; i < sizeof off / sizeof off[0]; i++) {
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_flash_write_page(TFW_BOOT_APP_BASE + off[i], data));
    }
    /* misaligned AND past the end: the alignment is checked first */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE,
                          tfw_boot_flash_write_page((uint32_t)TFW_APP_WRITE_END + 1u, data));
    TEST_ASSERT_EQUAL_UINT(0u, nev);
}

static void test_write_page_refuses_the_boot_section(void)
{
    static uint8_t data[256];
    uint32_t       last = (uint32_t)TFW_APP_WRITE_END - PAGE;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_boot_flash_write_page((uint32_t)TFW_APP_WRITE_END, data));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION,
                          tfw_boot_flash_write_page((uint32_t)TFW_APP_WRITE_END + PAGE, data));
    TEST_ASSERT_EQUAL_UINT(0u, nev);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_flash_write_page(last, data));   /* the last page is ours */
    TEST_ASSERT_EQUAL_HEX32(last, write_addr);
}

/* An address so high that addr + PAGE wraps to 0 or a small number must not
 * pass the boot-section guard: on the 2560 only the low bits reach SPM, so the
 * page would land in the bootloader. */
static void test_write_page_refuses_an_address_that_wraps(void)
{
    static uint8_t data[256];
    uint32_t       wrap[] = { 0u - PAGE, 0u - 2u * PAGE, 0x80000000u, (uint32_t)TFW_APP_WRITE_END + 2u * PAGE };
    unsigned       i;

    for (i = 0u; i < sizeof wrap / sizeof wrap[0]; i++) {
        TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_boot_flash_write_page(wrap[i], data));
    }
    TEST_ASSERT_EQUAL_UINT(0u, nev);
}

/* ------------------------------------------------------------------ nvm */

static void test_nvm_read_is_the_eeprom_from_zero(void)
{
    uint8_t buf[8];

    ee[0x10] = 0x11u; ee[0x17] = 0x77u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_nvm_read(0x10u, buf, 8u));
    TEST_ASSERT_EQUAL_STRING("R", ev);
    TEST_ASSERT_EQUAL_UINT(0x10u, ee_addr);
    TEST_ASSERT_EQUAL_size_t(8u, ee_len);
    TEST_ASSERT_EQUAL_HEX8(0x11u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x77u, buf[7]);
}

static void test_nvm_bounds_are_the_eeprom_size(void)
{
    uint8_t buf[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_nvm_read((uint16_t)(TFW_EEPROM_SIZE - 8u), buf, 8u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_nvm_write((uint16_t)(TFW_EEPROM_SIZE - 8u), buf, 8u));
    TEST_ASSERT_EQUAL_STRING("RU", ev);
    nev = 0u; memset(ev, 0, sizeof ev);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_nvm_read((uint16_t)(TFW_EEPROM_SIZE - 7u), buf, 8u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_nvm_write((uint16_t)(TFW_EEPROM_SIZE - 7u), buf, 8u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_nvm_read(0xFFFFu, buf, 1u));     /* no 16-bit wrap */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_nvm_write(0xFFFFu, buf, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_nvm_read((uint16_t)TFW_EEPROM_SIZE, buf, 0u + 1u));
    TEST_ASSERT_EQUAL_UINT(0u, nev);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_nvm_read((uint16_t)TFW_EEPROM_SIZE, buf, 0u));
}

static void test_nvm_write_updates_rather_than_writes(void)
{
    const uint8_t buf[4] = { 0xA1u, 0xB2u, 0xC3u, 0xD4u };

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_nvm_write(0x20u, buf, 4u));
    TEST_ASSERT_EQUAL_STRING("U", ev);
    TEST_ASSERT_EQUAL_UINT(0x20u, ee_addr);
    TEST_ASSERT_EQUAL_size_t(4u, ee_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(buf, &ee[0x20], 4u);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, ee[0x24]);
}

/* ----------------------------------------------------------------- uart */

static void test_uart_init_is_double_speed_8n1_polled(void)
{
    tfw_boot_uart_init(115200u);
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_UBRR0H]);
    TEST_ASSERT_EQUAL_HEX8(16u, regs[TFW_AVR_UBRR0L]);       /* 16e6 / (8 * 115200) - 1 */
    TEST_ASSERT_EQUAL_HEX8(0x02u, regs[TFW_AVR_UCSR0A]);     /* U2X0 */
    TEST_ASSERT_EQUAL_HEX8(0x06u, regs[TFW_AVR_UCSR0C]);     /* UCSZ01 | UCSZ00 */
    TEST_ASSERT_EQUAL_HEX8(0x18u, regs[TFW_AVR_UCSR0B]);     /* RXEN0 | TXEN0, no RXCIE0 */

    regs[TFW_AVR_UCSR0A] = 0xFFu;
    tfw_boot_uart_init(300u);                                /* 6665 = 0x1A09 */
    TEST_ASSERT_EQUAL_HEX8(0x1Au, regs[TFW_AVR_UBRR0H]);
    TEST_ASSERT_EQUAL_HEX8(0x09u, regs[TFW_AVR_UBRR0L]);
    TEST_ASSERT_EQUAL_HEX8(0x02u, regs[TFW_AVR_UCSR0A]);     /* assigned whole */
}

static void test_uart_rx_takes_a_byte_only_when_one_is_there(void)
{
    uint8_t b = 0xEEu;

    regs[TFW_AVR_UDR0]   = 0x42u;
    regs[TFW_AVR_UCSR0A] = 0x7Fu;                            /* everything but RXC0 */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, b);
    regs[TFW_AVR_UCSR0A] = 0x80u;
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_boot_uart_rx(&b));
    TEST_ASSERT_EQUAL_HEX8(0x42u, b);
}

static void test_uart_tx_writes_when_the_register_is_empty(void)
{
    regs[TFW_AVR_UCSR0A] = 0x20u;                            /* UDRE0 alone */
    tfw_boot_uart_tx(0x5Au);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, regs[TFW_AVR_UDR0]);
}

static void test_uart_tx_waits_for_the_register_to_empty(void)
{
    regs[TFW_AVR_UCSR0A] = 0xDFu;                            /* everything but UDRE0 */
    udre_in = 3u;                                            /* empty on the third look */
    tfw_boot_uart_tx(0xA5u);
    TEST_ASSERT_EQUAL_UINT(3u, ucsra_reads);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, regs[TFW_AVR_UDR0]);
}

/* -------------------------------------------------------------- handoff */

static void test_handoff_round_trips_through_its_section(void)
{
    uint8_t   in[TFW_BOOT_HANDOFF_LEN], out[TFW_BOOT_HANDOFF_LEN];
    uintptr_t at = 0u;
    unsigned  i;

    TEST_ASSERT_EQUAL_UINT64(TFW_BOOT_HANDOFF_LEN, section_size(".handoff", &at));
    for (i = 0u; i < sizeof in; i++) {
        in[i] = (uint8_t)(0xC0u ^ i);
    }
    tfw_boot_handoff_write(in, (uint8_t)sizeof in);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, (const uint8_t *)at, sizeof in);   /* in the section */
    memset(out, 0, sizeof out);
    tfw_boot_handoff_read(out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in, out, sizeof out);

    memset(in, 0x11, 4u);
    memset(&in[4], 0x22, sizeof in - 4u);                    /* not to be copied */
    tfw_boot_handoff_write(in, 4u);                          /* len bytes, from the start */
    memset(out, 0, sizeof out);
    tfw_boot_handoff_read(out, 6u);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x11u, out, 4u);
    TEST_ASSERT_EQUAL_HEX8(0xC4u, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0xC5u, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[6]);
    TEST_ASSERT_EQUAL_UINT(0u, nev);
}

/* -------------------------------------------------------------- control */

static void test_jump_resets_the_uart_then_calls_zero(void)
{
    struct sigaction sa, old_segv, old_bus;

    regs[TFW_AVR_UCSR0A] = 0x22u;
    regs[TFW_AVR_UCSR0B] = 0x18u;
    regs[TFW_AVR_UBRR0H] = 0x01u;
    regs[TFW_AVR_UBRR0L] = 0x10u;
    regs[TFW_AVR_UCSR0C] = 0x06u;
    regs[TFW_AVR_EIND]   = 0x01u;                            /* where the crt left it */

    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags     = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &old_segv);
    sigaction(SIGBUS, &sa, &old_bus);
    if (sigsetjmp(back, 1) == 0) {
        tfw_boot_jump_application();
    }
    sigaction(SIGSEGV, &old_segv, NULL);
    sigaction(SIGBUS, &old_bus, NULL);

    TEST_ASSERT_EQUAL_INT(SIGSEGV, fault_sig);
    TEST_ASSERT_EQUAL_HEX64(0u, fault_addr);                 /* a call to zero, nowhere else */
    TEST_ASSERT_EQUAL_HEX8(0u, regs[TFW_AVR_UCSR0A]);
    TEST_ASSERT_EQUAL_HEX8(0u, regs[TFW_AVR_UCSR0B]);
    TEST_ASSERT_EQUAL_HEX8(0u, regs[TFW_AVR_UBRR0H]);
    TEST_ASSERT_EQUAL_HEX8(0u, regs[TFW_AVR_UBRR0L]);
    TEST_ASSERT_EQUAL_HEX8(0x06u, regs[TFW_AVR_UCSR0C]);     /* not one the jump resets */
    TEST_ASSERT_EQUAL_HEX8(has_eind ? 0x00u : 0x01u, regs[TFW_AVR_EIND]);
    TEST_ASSERT_EQUAL_STRING(has_eind ? "rE" : "r", ev);     /* RWW readable, then EIND */
}

static void test_reset_bites_with_the_timed_watchdog_sequence(void)
{
    struct sigaction  sa, old;
    struct itimerval  t, off;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_tick;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, &old);
    memset(&t, 0, sizeof t);
    t.it_value.tv_usec    = 1000;
    t.it_interval.tv_usec = 1000;
    memset(&off, 0, sizeof off);
    regs[TFW_AVR_WDTCSR] = 0x40u;
    if (sigsetjmp(back, 1) == 0) {
        setitimer(ITIMER_PROF, &t, NULL);
        tfw_boot_reset();
    }
    setitimer(ITIMER_PROF, &off, NULL);
    sigaction(SIGPROF, &old, NULL);

    TEST_ASSERT_EQUAL_INT(2, ticks);                         /* it spun until the bite */
    TEST_ASSERT_EQUAL_STRING("c", ev);                       /* interrupts off first */
    TEST_ASSERT_EQUAL_UINT(2u, nwdt);
    TEST_ASSERT_EQUAL_HEX8(0x40u, wdt_seen[0]);
    TEST_ASSERT_EQUAL_HEX8(0x18u, wdt_seen[1]);              /* WDCE | WDE */
    TEST_ASSERT_EQUAL_HEX8(0x08u, regs[TFW_AVR_WDTCSR]);     /* WDE, shortest period */
}

static void test_watchdog_arm_is_8s(void)
{
    regs[TFW_AVR_WDTCSR] = 0x40u;
    tfw_boot_watchdog_arm();
    TEST_ASSERT_EQUAL_UINT(2u, nwdt);
    TEST_ASSERT_EQUAL_HEX8(0x18u, wdt_seen[1]);              /* WDCE | WDE */
    TEST_ASSERT_EQUAL_HEX8(0x29u, regs[TFW_AVR_WDTCSR]);     /* WDE | WDP3 | WDP0 */
    TEST_ASSERT_EQUAL_UINT(0u, nev);
}

static void test_reset_cause_is_the_early_latch(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_RESET_BROWNOUT, tfw_boot_reset_cause());
    TEST_ASSERT_EQUAL_STRING("x", ev);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flash_read_takes_the_parts_read_at_each_address);
    RUN_TEST(test_flash_read_of_nothing_reads_nothing);
    RUN_TEST(test_flash_page_size_is_the_parts);
    RUN_TEST(test_write_page_erases_fills_writes_in_order);
    RUN_TEST(test_write_page_restores_a_clear_flag_clear);
    RUN_TEST(test_write_page_refuses_a_misaligned_address);
    RUN_TEST(test_write_page_refuses_the_boot_section);
    RUN_TEST(test_write_page_refuses_an_address_that_wraps);
    RUN_TEST(test_nvm_read_is_the_eeprom_from_zero);
    RUN_TEST(test_nvm_bounds_are_the_eeprom_size);
    RUN_TEST(test_nvm_write_updates_rather_than_writes);
    RUN_TEST(test_uart_init_is_double_speed_8n1_polled);
    RUN_TEST(test_uart_rx_takes_a_byte_only_when_one_is_there);
    RUN_TEST(test_uart_tx_writes_when_the_register_is_empty);
    RUN_TEST(test_uart_tx_waits_for_the_register_to_empty);
    RUN_TEST(test_handoff_round_trips_through_its_section);
    RUN_TEST(test_jump_resets_the_uart_then_calls_zero);
    RUN_TEST(test_reset_bites_with_the_timed_watchdog_sequence);
    RUN_TEST(test_watchdog_arm_is_8s);
    RUN_TEST(test_reset_cause_is_the_early_latch);
    return UNITY_END();
}
