/*
 * boot/hal/host/boot_hal_host.c -- tfw-boot's HAL inside tfwsim (#52).
 *
 * The contract is boot/boot_hal.h, the same one boot/hal/avr/boot_hal_avr.c
 * keeps. What is shared with the firmware is shared the way the part shares it:
 *
 *   EEPROM   the mock HAL's (tfw_mock_eeprom), which the firmware's platform
 *            block reads and writes -- one array, not a copy each side.
 *   handoff  the mock HAL's handoff block, the SRAM both images agree on.
 *   store    the firmware stager's SPI NOR model (test/model/spi_nor_model.c),
 *            reached by the same transfer the AVR HAL makes over SPI.
 *   flash    a buffer here: the application region the stage copies into and
 *            boot_policy.c measures and verifies.
 *
 * OTA over the boot UART is off, as on an image built with BOOT_OTA=0: the UART
 * never has a byte, recovery reports and stops, and a session is never opened
 * (tfw_boot_session_open below is recovery_halt.c's no-op). Every exit from a
 * pass -- the jump, tfw_boot_reset, recovery -- returns to tfw_boot_host_pass.
 */
#include <setjmp.h>
#include <string.h>

#include "boot_hal.h"
#include "boot_host.h"
#include "boot_recovery.h"
#include "hal_mock.h"
#include "spi_nor_model.h"

/* SPM_PAGESIZE on the 328P, the host board (boards/atmega328p). */
#define HOST_PAGE 128u

static uint8_t           flash[TFW_FLASH_TOTAL];
static uint8_t           flash_ready;
static tfw_reset_cause_t cause = TFW_RESET_POWER_ON;
static tfw_status_t      recovery_reason;
static jmp_buf           pass_exit;

uint8_t *tfw_boot_host_flash(void)
{
    if (flash_ready == 0u) {
        memset(flash, 0xFF, sizeof flash);
        flash_ready = 1u;
    }
    return flash;
}

void tfw_boot_host_set_cause(tfw_reset_cause_t c) { cause = c; }

tfw_status_t tfw_boot_host_recovery_reason(void) { return recovery_reason; }

tfw_boot_host_exit_t tfw_boot_host_pass(int (*entry)(void))
{
    int how = setjmp(pass_exit);

    if (how == 0) {
        (void)tfw_boot_host_flash();
        (void)entry();
        /* main() returns only past the jump, which does not return. */
        how = TFW_BOOT_HOST_RECOVERY;
        recovery_reason = TFW_ERR_INTERNAL;
    }
    return (tfw_boot_host_exit_t)how;
}

void tfw_boot_flash_read(uint32_t addr, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    for (i = 0u; i < len; i++) {
        buf[i] = (addr + i < (uint32_t)TFW_FLASH_TOTAL) ? tfw_boot_host_flash()[addr + i] : 0xFFu;
    }
}

uint16_t tfw_boot_flash_page_size(void) { return HOST_PAGE; }

tfw_status_t tfw_boot_flash_write_page(uint32_t addr, const uint8_t *data)
{
    if ((addr % HOST_PAGE) != 0u) {
        return TFW_ERR_RANGE;
    }
    /* The AVR HAL's refusal: never a page over the bootloader. */
    if (addr + HOST_PAGE > (uint32_t)TFW_APP_WRITE_END) {
        return TFW_ERR_PROTECTED_REGION;
    }
    memcpy(&tfw_boot_host_flash()[addr], data, HOST_PAGE);
    return TFW_OK;
}

tfw_status_t tfw_boot_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((size_t)addr + len > tfw_mock_eeprom_size()) {
        return TFW_ERR_RANGE;
    }
    memcpy(buf, &tfw_mock_eeprom()[addr], len);
    return TFW_OK;
}

tfw_status_t tfw_boot_nvm_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    if ((size_t)addr + len > tfw_mock_eeprom_size()) {
        return TFW_ERR_RANGE;
    }
    memcpy(&tfw_mock_eeprom()[addr], buf, len);
    return TFW_OK;
}

tfw_status_t tfw_boot_store_open(void) { return TFW_OK; }

/* One transaction on the part, as boot_hal_mock.c frames it: the command, then
 * ilen bytes clocked in behind it. */
tfw_status_t tfw_boot_store_cmd(const uint8_t *cmd, uint8_t clen, uint8_t *in, uint16_t ilen)
{
    uint8_t t[4u + 512u];

    if ((uint32_t)clen + ilen > sizeof t) {
        return TFW_ERR_RANGE;
    }
    memcpy(t, cmd, clen);
    memset(&t[clen], 0, ilen);
    tfw_nor_model_xfer(t, (uint16_t)(clen + ilen));
    memcpy(in, &t[clen], ilen);
    return TFW_OK;
}

void tfw_boot_store_close(void) { }

void tfw_boot_uart_init(uint32_t baud) { (void)baud; }

uint8_t tfw_boot_uart_rx(uint8_t *out)
{
    (void)out;
    return 0u;
}

void tfw_boot_uart_tx(uint8_t byte) { (void)byte; }

void tfw_boot_handoff_write(const uint8_t *block, uint8_t len)
{
    memcpy(tfw_mock_handoff(), block, len <= TFW_BOOT_HANDOFF_LEN ? len : TFW_BOOT_HANDOFF_LEN);
}

void tfw_boot_handoff_read(uint8_t *buf, uint8_t len)
{
    memcpy(buf, tfw_mock_handoff(), len <= TFW_BOOT_HANDOFF_LEN ? len : TFW_BOOT_HANDOFF_LEN);
}

tfw_reset_cause_t tfw_boot_reset_cause(void) { return cause; }

void tfw_boot_jump_application(void) { longjmp(pass_exit, TFW_BOOT_HOST_JUMPED); }

void tfw_boot_reset(void) { longjmp(pass_exit, TFW_BOOT_HOST_RESET); }

/* The firmware's trial deadline is its own (src/core/ota_trial.c), on the mock
 * clock; there is no watchdog to arm under it. */
void tfw_boot_watchdog_arm(void) { }

_Noreturn void tfw_boot_recovery(tfw_boot_platform_t *plat, tfw_status_t reason)
{
    (void)plat;
    recovery_reason = reason;
    longjmp(pass_exit, TFW_BOOT_HOST_RECOVERY);
}

void tfw_boot_session_open(tfw_boot_platform_t *plat, const tfw_boot_session_limits_t *lim)
{
    (void)plat;
    (void)lim;
}
