/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The feature-off stubs no other suite links: bus_none.c, handoff_seed_none.c,
 * ota_manifest_none.c, ota_prev_none.c, ota_stage_none.c and ota_trial_none.c.
 * A build with the feature off links the stub, so the stub is the whole of that
 * build's behaviour: the answer it gives, and everything it leaves alone. The
 * link is the stubs and the HAL mock and nothing else, so what the mock saw is
 * all the stub could have done.
 */

#include <string.h>
#include "unity.h"
#include "bus.h"
#include "handoff.h"
#include "ota_stage.h"
#include "ota_trial.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "board_profile.h"

#define FLASH_LEN  (32u * 1024u)
#define EEPROM_CAP 4096u

static const uint16_t ports[] = { TFW_ADDR_PINB, TFW_ADDR_PINC, TFW_ADDR_PIND };
#define NPORTS (sizeof ports / sizeof ports[0])

/* Everything the mock HAL lets a test observe, taken before and after. */
typedef struct {
    uint8_t  flash[FLASH_LEN];
    uint8_t  eeprom[EEPROM_CAP];
    size_t   ee_writes;
    uint8_t  pin_out[NPORTS][8];
    uint8_t  pin_level[NPORTS][8];
    uint8_t  pin_pullup[NPORTS][8];
    size_t   tx_pending;
    uint16_t tone_hz;
    uint32_t i2c_hz;
    uint8_t  spi_mode, spi_clkdiv, spi_lsb;
    unsigned resets, wdt_arms, wdt_pets;
    uint8_t  irq;
} hal_seen_t;

static hal_seen_t before, after;

static void take(hal_seen_t *s)
{
    uint16_t lo = 0u, hi = 0u;
    size_t   p;
    uint8_t  b;

    memset(s, 0, sizeof *s);
    memcpy(s->flash, tfw_mock_flash(), FLASH_LEN);
    memcpy(s->eeprom, tfw_mock_eeprom(), tfw_mock_eeprom_size());
    s->ee_writes = tfw_mock_eeprom_written(&lo, &hi);
    for (p = 0u; p < NPORTS; p++) {
        for (b = 0u; b < 8u; b++) {
            s->pin_out[p][b]    = tfw_mock_pin_is_output(ports[p], b);
            s->pin_level[p][b]  = tfw_mock_pin_level(ports[p], b);
            s->pin_pullup[p][b] = tfw_mock_pin_pullup(ports[p], b);
        }
    }
    s->tx_pending = tfw_mock_tx_pending();
    s->tone_hz    = tfw_mock_tone_hz();
    s->i2c_hz     = tfw_mock_last_i2c_hz();
    s->spi_mode   = tfw_mock_last_spi_mode();
    s->spi_clkdiv = tfw_mock_last_spi_clkdiv();
    s->spi_lsb    = tfw_mock_last_spi_lsb();
    s->resets     = tfw_mock_reset_count();
    s->wdt_arms   = tfw_mock_watchdog_arms();
    s->wdt_pets   = tfw_mock_watchdog_pets();
    s->irq        = tfw_mock_irq_enabled();
}

/* The stub under test touched nothing the HAL can see. */
static void assert_hal_untouched(void)
{
    take(&after);
    TEST_ASSERT_EQUAL_MEMORY(before.flash, after.flash, FLASH_LEN);
    TEST_ASSERT_EQUAL_MEMORY(before.eeprom, after.eeprom, EEPROM_CAP);
    TEST_ASSERT_EQUAL_size_t(before.ee_writes, after.ee_writes);
    TEST_ASSERT_EQUAL_MEMORY(before.pin_out, after.pin_out, sizeof before.pin_out);
    TEST_ASSERT_EQUAL_MEMORY(before.pin_level, after.pin_level, sizeof before.pin_level);
    TEST_ASSERT_EQUAL_MEMORY(before.pin_pullup, after.pin_pullup, sizeof before.pin_pullup);
    TEST_ASSERT_EQUAL_size_t(before.tx_pending, after.tx_pending);
    TEST_ASSERT_EQUAL_UINT16(before.tone_hz, after.tone_hz);
    TEST_ASSERT_EQUAL_UINT32(before.i2c_hz, after.i2c_hz);
    TEST_ASSERT_EQUAL_UINT8(before.spi_mode, after.spi_mode);
    TEST_ASSERT_EQUAL_UINT8(before.spi_clkdiv, after.spi_clkdiv);
    TEST_ASSERT_EQUAL_UINT8(before.spi_lsb, after.spi_lsb);
    TEST_ASSERT_EQUAL_UINT(before.resets, after.resets);
    TEST_ASSERT_EQUAL_UINT(before.wdt_arms, after.wdt_arms);
    TEST_ASSERT_EQUAL_UINT(before.wdt_pets, after.wdt_pets);
    TEST_ASSERT_EQUAL_UINT8(before.irq, after.irq);
}

void setUp(void)
{
    tfw_mock_reset();
    TEST_ASSERT_TRUE(tfw_mock_eeprom_size() <= EEPROM_CAP);
    /* A canary in both stores, so a write of any value shows. */
    memset(tfw_mock_flash(), 0xA5, FLASH_LEN);
    memset(tfw_mock_eeprom(), 0x5A, tfw_mock_eeprom_size());
    take(&before);
}
void tearDown(void) {}

/* bus_none.c: no I2C, SPI or 1-Wire, so the boot defaults go nowhere -- the
 * mock's never-initialised sentinels survive. */
static void test_with_no_bus_the_bus_init_initialises_no_bus_and_touches_nothing(void)
{
    tfw_bus_init();
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFuL, tfw_mock_last_i2c_hz());
    TEST_ASSERT_EQUAL_UINT8(0xFFu, tfw_mock_last_spi_mode());
    TEST_ASSERT_EQUAL_UINT8(0xFFu, tfw_mock_last_spi_clkdiv());
    TEST_ASSERT_EQUAL_UINT8(0xFFu, tfw_mock_last_spi_lsb());
    assert_hal_untouched();
}

/* handoff_seed_none.c: no DeviceID, no store -- NULL, every time, which tells
 * tfw_boot_handoff_init to keep no seed. */
static void test_with_no_device_id_the_seed_store_is_null_and_touches_nothing(void)
{
    TEST_ASSERT_NULL(tfw_boot_handoff_seed_store());
    TEST_ASSERT_NULL(tfw_boot_handoff_seed_store());
    assert_hal_untouched();
}

/* ota_manifest_none.c: no SUIT reader, so OTA_ACTIVATE holds the image to the
 * signature and trailer alone -- TFW_OK, and the platform block as it was. */
static void test_with_the_manifest_reader_off_activation_passes_and_leaves_the_platform_block_unwritten(void)
{
    tfw_boot_platform_t p;
    tfw_boot_platform_t was;

    memset(&p, 0xC3, sizeof p);
    memset(&was, 0xC3, sizeof was);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_ota_manifest(&p));
    TEST_ASSERT_EQUAL_MEMORY(&was, &p, sizeof p);
    assert_hal_untouched();
}

/* ota_prev_none.c: the fallback is a second internal slot, so there is no PREV
 * to copy the running image into -- TFW_OK, and no store or flash written. */
static void test_with_no_prev_slot_the_backup_passes_and_writes_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_ota_prev_backup());
    assert_hal_untouched();
}

/* ota_stage_none.c: no store, so no select to park -- no pin moves. */
static void test_with_no_stager_the_stage_init_parks_no_select(void)
{
    tfw_stage_init();
    assert_hal_untouched();
}

/* ota_stage_none.c: no stager wrote a MARK, so every reset is a plain one. */
static void test_with_no_stager_no_install_is_ever_pending(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_stage_install_pending());
    tfw_stage_init();
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_stage_install_pending());
    assert_hal_untouched();
}

/* ota_trial_none.c: no boot section, no trial -- nothing read, no watchdog
 * armed, no deadline started. */
static void test_with_no_boot_section_the_trial_start_arms_nothing(void)
{
    tfw_ota_trial_start();
    assert_hal_untouched();
}

/* ota_trial_none.c: no trial, so nothing to confirm after init. */
static void test_with_no_boot_section_the_trial_after_init_confirms_nothing(void)
{
    tfw_ota_trial_after_init();
    assert_hal_untouched();
}

/* ota_trial_none.c: no deadline, so the poll never resets or pets, however
 * long the device runs. */
static void test_with_no_boot_section_the_trial_poll_never_resets(void)
{
    tfw_ota_trial_start();
    tfw_mock_advance(60000u);
    tfw_ota_trial_poll();
    tfw_mock_advance(60000u);
    tfw_ota_trial_poll();
    assert_hal_untouched();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_with_no_bus_the_bus_init_initialises_no_bus_and_touches_nothing);
    RUN_TEST(test_with_no_device_id_the_seed_store_is_null_and_touches_nothing);
    RUN_TEST(test_with_the_manifest_reader_off_activation_passes_and_leaves_the_platform_block_unwritten);
    RUN_TEST(test_with_no_prev_slot_the_backup_passes_and_writes_nothing);
    RUN_TEST(test_with_no_stager_the_stage_init_parks_no_select);
    RUN_TEST(test_with_no_stager_no_install_is_ever_pending);
    RUN_TEST(test_with_no_boot_section_the_trial_start_arms_nothing);
    RUN_TEST(test_with_no_boot_section_the_trial_after_init_confirms_nothing);
    RUN_TEST(test_with_no_boot_section_the_trial_poll_never_resets);
    return UNITY_END();
}
