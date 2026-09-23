/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "boot_hal_mock.h"
#include "boot_hal.h"

/* The 328P's page size, because the host build is configured for that board and
 * a mock that invented its own would let an address bug pass every test. */
#define MOCK_PAGE 128u

static uint8_t  flash[AG_FLASH_TOTAL];
static uint8_t  eeprom[AG_EEPROM_SIZE];
static uint8_t  handoff_block[AG_BOOT_HANDOFF_LEN];

static uint8_t  rx[4096];
static size_t   rx_head, rx_tail;
static uint8_t  tx[4096];
static size_t   tx_n;

static unsigned page_writes;
static uint32_t last_page;
static unsigned fail_writes;
static unsigned jumped;
static unsigned reset_calls;

void ag_boot_mock_reset(void)
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
    jumped = 0u;
    reset_calls = 0u;
}

uint8_t *ag_boot_mock_flash(void)     { return flash; }
size_t   ag_boot_mock_flash_size(void){ return sizeof flash; }
uint8_t *ag_boot_mock_eeprom(void)    { return eeprom; }
unsigned ag_boot_mock_page_writes(void)   { return page_writes; }
uint32_t ag_boot_mock_last_page_addr(void){ return last_page; }
void     ag_boot_mock_fail_writes(unsigned n) { fail_writes = n; }
const uint8_t *ag_boot_mock_handoff(void) { return handoff_block; }
unsigned ag_boot_mock_jumped(void)    { return jumped; }
unsigned ag_boot_mock_did_reset(void) { return reset_calls; }

void ag_boot_mock_feed_rx(const uint8_t *b, size_t n)
{
    for (size_t i = 0u; i < n && rx_head < sizeof rx; i++) {
        rx[rx_head++] = b[i];
    }
}

size_t ag_boot_mock_drain_tx(uint8_t *out, size_t cap)
{
    size_t n = (tx_n < cap) ? tx_n : cap;
    memcpy(out, tx, n);
    tx_n = 0u;
    return n;
}

/* ---------------------------------------------------------- the contract */

void ag_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    for (uint8_t i = 0u; i < len; i++) {
        uint32_t a = addr + i;
        buf[i] = (a < sizeof flash) ? flash[a] : 0xFFu;
    }
}

uint16_t ag_boot_flash_page_size(void) { return MOCK_PAGE; }

ag_status_t ag_boot_flash_write_page(uint32_t addr, const uint8_t *data)
{
    if ((addr % MOCK_PAGE) != 0u) {
        return AG_ERR_RANGE;
    }
    /* The same refusal the AVR HAL makes, and it is here rather than only there
     * so the test suite exercises it: an OTA image one page too long would
     * otherwise overwrite the bootloader. */
    if (addr + MOCK_PAGE > AG_BOOT_APP_END) {
        return AG_ERR_PROTECTED_REGION;
    }
    if (fail_writes > 0u) {
        fail_writes--;
        return AG_ERR_INTERNAL;
    }
    memcpy(&flash[addr], data, MOCK_PAGE);
    page_writes++;
    last_page = addr;
    return AG_OK;
}

ag_status_t ag_boot_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > sizeof eeprom) {
        return AG_ERR_RANGE;
    }
    memcpy(buf, &eeprom[addr], len);
    return AG_OK;
}

ag_status_t ag_boot_eeprom_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((uint32_t)addr + len > sizeof eeprom) {
        return AG_ERR_RANGE;
    }
    memcpy(&eeprom[addr], buf, len);
    return AG_OK;
}

void ag_boot_uart_init(uint32_t baud) { (void)baud; }

uint8_t ag_boot_uart_rx(uint8_t *out)
{
    if (rx_tail >= rx_head) {
        return 0u;
    }
    *out = rx[rx_tail++];
    return 1u;
}

void ag_boot_uart_tx(uint8_t byte)
{
    if (tx_n < sizeof tx) {
        tx[tx_n++] = byte;
    }
}

void ag_boot_handoff_write(const uint8_t *block, uint8_t len)
{
    memcpy(handoff_block, block, len);
}

void ag_boot_jump_application(void) { jumped++; }
void ag_boot_reset(void)            { reset_calls++; }
