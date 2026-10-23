/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <limits.h>
#include <string.h>

#include "boot_hal_mock.h"
#include "boot_hal.h"

/* The 328P's page size, because the host build is configured for that board and
 * a mock that invented its own would let an address bug pass every test. */
#define MOCK_PAGE 128u

static uint8_t  flash[TFW_FLASH_TOTAL];
static uint8_t  eeprom[TFW_EEPROM_SIZE];
static uint8_t  handoff_block[TFW_BOOT_HANDOFF_LEN];

static uint8_t  rx[4096];
static size_t   rx_head, rx_tail;
static uint8_t  tx[4096];
static size_t   tx_n;

static unsigned page_writes;
static uint32_t last_page;
static unsigned fail_writes;
static unsigned cut_left = UINT_MAX;
static unsigned tear_left = UINT_MAX;
static uint8_t  tear_byte;
static uint8_t  tear_val;
static uint8_t  torn;
static unsigned nvm_read_fails;
static unsigned nvm_lost_writes;
static void   (*g_store)(uint8_t *buf, uint16_t len);
static uint16_t page_size = MOCK_PAGE;
static unsigned jumped;
static unsigned reset_calls;
/* Ordinals on one counter, so a test can assert the order of the last NVM
 * write, the watchdog arm and the jump; 0 is "never". */
static unsigned events, nvm_at, wdt_at, jump_at;
static unsigned empty_polls;
static unsigned consumed;
static void   (*rx_idle_hook)(unsigned);
static tfw_reset_cause_t cause;

void tfw_boot_mock_reset(void)
{
    /* Flash starts ERASED, not zeroed. An erased AVR reads 0xFF, and a test
     * that began from zeros would be testing against a state no real part is
     * ever in -- including the "blank device" case, which is the one where the
     * difference decides whether a trailer parse finds a magic or not. */
    memset(flash, 0xFF, sizeof flash);
    memset(eeprom, 0xFF, sizeof eeprom);
    memset(handoff_block, 0, sizeof handoff_block);
    rx_head = rx_tail = 0u;
    tx_n = 0u;
    page_writes = 0u;
    last_page = 0u;
    fail_writes = 0u;
    cut_left = UINT_MAX;
    tear_left = UINT_MAX;
    torn = 0u;
    nvm_read_fails = 0u;
    nvm_lost_writes = 0u;
    g_store = 0;
    jumped = 0u;
    reset_calls = 0u;
    events = nvm_at = wdt_at = jump_at = 0u;
    empty_polls = 0u;
    consumed = 0u;
    rx_idle_hook = 0;
    cause = TFW_RESET_UNKNOWN;
    page_size = MOCK_PAGE;
}

uint8_t *tfw_boot_mock_flash(void)     { return flash; }
size_t   tfw_boot_mock_flash_size(void){ return sizeof flash; }
uint8_t *tfw_boot_mock_nvm(void)    { return eeprom; }
unsigned tfw_boot_mock_page_writes(void)   { return page_writes; }
uint32_t tfw_boot_mock_last_page_addr(void){ return last_page; }
void     tfw_boot_mock_fail_writes(unsigned n) { fail_writes = n; }
void     tfw_boot_mock_cut_after(unsigned ok)  { cut_left = ok; }

void tfw_boot_mock_tear_nvm(unsigned ok, uint8_t byte, uint8_t val)
{
    tear_left = ok;
    tear_byte = byte;
    tear_val  = val;
    torn      = 0u;
    if (ok == UINT_MAX) {
        cut_left = UINT_MAX;
    }
}

void tfw_boot_mock_fail_nvm_reads(unsigned n)
{
    nvm_read_fails = n;
}

void tfw_boot_mock_lose_nvm_writes(unsigned n)
{
    nvm_lost_writes = n;
}
const uint8_t *tfw_boot_mock_handoff(void) { return handoff_block; }

void tfw_boot_mock_set_handoff(const uint8_t *b, size_t n)
{
    memcpy(handoff_block, b, n);
}
unsigned tfw_boot_mock_rx_consumed(void) { return consumed; }
unsigned tfw_boot_mock_rx_empty_polls(void) { return empty_polls; }
void tfw_boot_mock_on_rx_idle(void (*hook)(unsigned empty_polls)) { rx_idle_hook = hook; }
unsigned tfw_boot_mock_jumped(void)    { return jumped; }
unsigned tfw_boot_mock_did_reset(void) { return reset_calls; }

void tfw_boot_mock_feed_rx(const uint8_t *b, size_t n)
{
    /* A drained queue starts over, so a test can stream far more than the
     * buffer holds as long as it never queues more than that at once. */
    if (rx_tail == rx_head) {
        rx_tail = rx_head = 0u;
    }
    for (size_t i = 0u; i < n && rx_head < sizeof rx; i++) {
        rx[rx_head++] = b[i];
    }
}

size_t tfw_boot_mock_drain_tx(uint8_t *out, size_t cap)
{
    size_t n = (tx_n < cap) ? tx_n : cap;
    memcpy(out, tx, n);
    tx_n = 0u;
    return n;
}

/* ---------------------------------------------------------- the contract */

void tfw_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    /* The part inside the mock flash in one copy, erased bytes past its end:
     * the swap suites read every unit of both slots on every cut (#163). */
    size_t in = (addr >= sizeof flash) ? 0u
              : (sizeof flash - addr < len) ? sizeof flash - addr : len;

    memcpy(buf, &flash[addr < sizeof flash ? addr : 0u], in);
    memset(&buf[in], 0xFF, len - in);
}

/* ---------------------------------------------------------------- store */

void tfw_boot_mock_on_store(void (*xfer)(uint8_t *buf, uint16_t len))
{
    g_store = xfer;
}

tfw_status_t tfw_boot_store_open(void)
{
    return (g_store != 0) ? TFW_OK : TFW_ERR_UNSUPPORTED;
}

tfw_status_t tfw_boot_store_cmd(const uint8_t *cmd, uint8_t clen, uint8_t *in, uint16_t ilen)
{
    uint8_t t[4u + 512u];

    if (g_store == 0) {
        return TFW_ERR_UNSUPPORTED;
    }
    if ((uint32_t)clen + ilen > sizeof t) {
        return TFW_ERR_RANGE;
    }
    memcpy(t, cmd, clen);
    memset(&t[clen], 0, ilen);
    g_store(t, (uint16_t)(clen + ilen));
    memcpy(in, &t[clen], ilen);
    return TFW_OK;
}

void tfw_boot_store_close(void)
{
}

uint16_t tfw_boot_flash_page_size(void) { return page_size; }

void tfw_boot_mock_set_page_size(uint16_t ps) { page_size = ps; }

tfw_status_t tfw_boot_flash_write_page(uint32_t addr, const uint8_t *data)
{
    if ((addr % MOCK_PAGE) != 0u) {
        return TFW_ERR_RANGE;
    }
    /* The same refusal the AVR HAL makes, and it is here rather than only there
     * so the test suite exercises it: an OTA image one page too long would
     * otherwise overwrite the bootloader. */
    if (addr + MOCK_PAGE > (uint32_t)TFW_APP_WRITE_END) {
        return TFW_ERR_PROTECTED_REGION;
    }
    if (fail_writes > 0u) {
        fail_writes--;
        return TFW_ERR_INTERNAL;
    }
    if (cut_left == 0u) {
        return TFW_ERR_INTERNAL;
    }
    cut_left--;
    memcpy(&flash[addr], data, MOCK_PAGE);
    page_writes++;
    last_page = addr;
    return TFW_OK;
}

tfw_status_t tfw_boot_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > sizeof eeprom) {
        return TFW_ERR_RANGE;
    }
    if (nvm_read_fails != 0u) {
        nvm_read_fails--;
        return TFW_ERR_INTERNAL;
    }
    memcpy(buf, &eeprom[addr], len);
    return TFW_OK;
}

tfw_status_t tfw_boot_nvm_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > sizeof eeprom) {
        return TFW_ERR_RANGE;
    }
    if (torn != 0u) {
        return TFW_ERR_INTERNAL;
    }
    if (tear_left == 0u) {
        if (tear_byte < len) {
            memcpy(&eeprom[addr], buf, tear_byte);
            eeprom[addr + tear_byte] = tear_val;
        }
        torn     = 1u;
        cut_left = 0u;
        return TFW_ERR_INTERNAL;
    }
    if (tear_left != UINT_MAX) {
        tear_left--;
    }
    if (nvm_lost_writes != 0u) {
        nvm_lost_writes--;
        return TFW_OK;
    }
    memcpy(&eeprom[addr], buf, len);
    nvm_at = ++events;
    return TFW_OK;
}

void tfw_boot_uart_init(uint32_t baud) { (void)baud; }

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    if (rx_tail >= rx_head && rx_idle_hook) {
        rx_idle_hook(empty_polls);
    }
    if (rx_tail >= rx_head) {
        empty_polls++;
        return 0u;
    }
    *out = rx[rx_tail++];
    consumed++;
    return 1u;
}

void tfw_boot_uart_tx(uint8_t byte)
{
    if (tx_n < sizeof tx) {
        tx[tx_n++] = byte;
    }
}

void tfw_boot_handoff_write(const uint8_t *block, uint8_t len)
{
    memcpy(handoff_block, block, len);
}

void tfw_boot_handoff_read(uint8_t *buf, uint8_t len)
{
    memcpy(buf, handoff_block, len);
}

void tfw_boot_jump_application(void) { jumped++; jump_at = ++events; }
void tfw_boot_reset(void)            { reset_calls++; }
void tfw_boot_watchdog_arm(void)     { wdt_at = ++events; }

unsigned tfw_boot_mock_nvm_write_at(void) { return nvm_at; }
unsigned tfw_boot_mock_watchdog_at(void)  { return wdt_at; }
unsigned tfw_boot_mock_jump_at(void)      { return jump_at; }

tfw_reset_cause_t tfw_boot_reset_cause(void)      { return cause; }
void tfw_boot_mock_set_reset_cause(tfw_reset_cause_t c) { cause = c; }
