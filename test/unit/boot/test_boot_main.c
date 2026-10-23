/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The one trial through tfw-boot's own main() (#77). test_boot_trial pins each
 * transition alone; this suite runs the real boot/boot_main.c, renamed by its
 * link in mk/boot.mk, over the mock HAL, so the ORDER main() calls them in is
 * what is tested: a pending image armed before the watchdog and the jump, an
 * unconfirmed trial failed at the next reset, a confirmed one settled to the
 * floor, an image under the floor refused, and a reset before any write of a
 * round leaving a device that boots a verified image or serves recovery with
 * its key still pinned.
 *
 * The row is the host board's, the 328P's in-place one: boot_stage_none.c and
 * boot_swap_none.c, the soft floor in EEPROM, and recovery and the session
 * played by this file: recovery leaves main() by longjmp where the device
 * would serve its loop. The
 * 2560's swap and its confirm request are test_boot_swap's.
 */
#include <limits.h>
#include <setjmp.h>
#include <string.h>
#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_verify_mock.h"
#include "boot_cdi.h"
#include "boot_image.h"
#include "boot_measure.h"
#include "boot_platform.h"
#include "boot_recovery.h"
#include "boot_store.h"
#include "sha256.h"

#define IMAGE_LEN 2048u

/* boot_main.c's main(), under the name its test link gives it. */
int tfw_boot_main(void);

static const uint8_t GOOD_SIG[TFW_BOOT_SIG_LEN] = { 0x5Au, 0xA5u };
static const uint8_t BAD_SIG[TFW_BOOT_SIG_LEN]  = { 0xA5u, 0x5Au };

static jmp_buf      escape;
static tfw_status_t recovered;
static uint8_t      handed[TFW_BOOT_MEASURE_LEN];
static uint8_t      image_meas[TFW_BOOT_MEASURE_LEN];

/* Recovery: where the device would serve its loop without a limit. */
_Noreturn void tfw_boot_recovery(tfw_boot_platform_t *plat, tfw_status_t reason)
{
    (void)plat;
    recovered = reason;
    longjmp(escape, 1);
}

/* No update request reaches these resets: a power-on cause honours none. */
void tfw_boot_session_open(tfw_boot_platform_t *plat, const tfw_boot_session_limits_t *lim)
{
    (void)plat;
    (void)lim;
    TEST_FAIL_MESSAGE("no reset in this suite asks for a session");
}

/* The soft tier with no UDS: the image still boots, and the measurement main()
 * hands over is kept, so a test sees which image ran. */
tfw_status_t tfw_boot_cdi_init(void) { return TFW_OK; }
uint8_t      tfw_boot_cdi_tier(void) { return 0u; }

tfw_status_t tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                               uint8_t cdi[TFW_BOOT_MEASURE_LEN])
{
    memcpy(handed, measurement, TFW_BOOT_MEASURE_LEN);
    memset(cdi, 0, TFW_BOOT_MEASURE_LEN);
    return TFW_ERR_NOT_PROVISIONED;
}

tfw_status_t tfw_boot_cdi_device_seed(uint8_t seed[TFW_BOOT_SEED_LEN])
{
    memset(seed, 0, TFW_BOOT_SEED_LEN);
    return TFW_ERR_NOT_PROVISIONED;
}

/* No element: the floor's authority is the EEPROM copy. */
tfw_status_t tfw_boot_floor_read(uint32_t *out)  { (void)out; return TFW_ERR_UNSUPPORTED; }
tfw_status_t tfw_boot_floor_write(uint32_t value) { (void)value; return TFW_ERR_UNSUPPORTED; }

enum { BOOTED, RECOVERED };

/* One reset: main() runs to the jump, or to recovery. */
static int boot(void)
{
    unsigned jumps = tfw_boot_mock_jumped();

    memset(handed, 0, sizeof handed);
    recovered = TFW_OK;
    if (setjmp(escape) == 0) {
        (void)tfw_boot_main();
        TEST_ASSERT_EQUAL_UINT(jumps + 1u, tfw_boot_mock_jumped());
        return BOOTED;
    }
    TEST_ASSERT_EQUAL_UINT(jumps, tfw_boot_mock_jumped());
    return RECOVERED;
}

/* A signed image of `version` in the application region. */
static void install(uint32_t version, const uint8_t *sig)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  raw[TFW_BOOT_TRAILER_LEN];
    tfw_boot_trailer_t tr;

    for (uint16_t i = 0u; i < IMAGE_LEN; i++) {
        flash[TFW_BOOT_APP_BASE + i] = (uint8_t)(version ^ i);
    }
    memset(&tr, 0, sizeof tr);
    tr.image_version = version;
    tr.image_len     = IMAGE_LEN;
    tr.board_id      = (uint8_t)TFW_BOARD_ID;
    tfw_sha256(&flash[TFW_BOOT_APP_BASE], IMAGE_LEN, tr.measurement);
    memcpy(image_meas, tr.measurement, sizeof image_meas);
    memcpy(tr.signature, sig, TFW_BOOT_SIG_LEN);
    tfw_boot_trailer_serialise(&tr, raw);
    memcpy(&flash[TFW_BOOT_TRAILER_ADDR], raw, TFW_BOOT_TRAILER_LEN);
}

/* The platform block, the mock verifier's all-zero key pinned. */
static void block(uint8_t boot_state, uint8_t ota_state, uint32_t floor)
{
    uint8_t ee[TFW_BOOT_EE_LEN];
    uint8_t key[TFW_BOOT_PUBKEY_LEN];
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    memset(key, 0, sizeof key);
    tfw_boot_hash(key, TFW_BOOT_PUBKEY_LEN, p.pubkey_hash);
    p.boot_state     = boot_state;
    p.ota_state      = ota_state;
    p.rollback_floor = floor;
    tfw_boot_platform_serialise(&p, ee);
    memcpy(tfw_boot_mock_nvm() + TFW_BOOT_EE_BASE, ee, TFW_BOOT_EE_LEN);
}

static tfw_boot_platform_t loaded(void)
{
    tfw_boot_platform_t p;
    tfw_status_t st;

    memset(&p, 0, sizeof p);
    tfw_boot_platform_load(&p, &st);
    TEST_ASSERT_EQUAL(TFW_OK, st);
    return p;
}

static void assert_block(uint8_t boot_state, uint8_t ota_state, uint32_t floor)
{
    tfw_boot_platform_t p = loaded();

    TEST_ASSERT_EQUAL_UINT8(boot_state, p.boot_state);
    TEST_ASSERT_EQUAL_UINT8(ota_state, p.ota_state);
    TEST_ASSERT_EQUAL_UINT32(floor, p.rollback_floor);
}

static void assert_ran_the_image(void)
{
    TEST_ASSERT_EQUAL_UINT8_ARRAY(image_meas, handed, TFW_BOOT_MEASURE_LEN);
}

void setUp(void)
{
    tfw_boot_mock_reset();
    tfw_boot_verify_mock_want(GOOD_SIG);
}

void tearDown(void)
{
    tfw_boot_verify_mock_want(NULL);
}

void test_a_good_image_boots_and_writes_no_block(void)
{
    uint8_t before[TFW_BOOT_EE_LEN];

    install(1u, GOOD_SIG);
    block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 1u);
    memcpy(before, tfw_boot_mock_nvm() + TFW_BOOT_EE_BASE, sizeof before);

    TEST_ASSERT_EQUAL(BOOTED, boot());
    assert_ran_the_image();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, tfw_boot_mock_nvm() + TFW_BOOT_EE_BASE,
                                  TFW_BOOT_EE_LEN);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_watchdog_at());
}

void test_a_new_image_boots_on_trial_armed_before_the_watchdog_and_the_jump(void)
{
    install(2u, GOOD_SIG);
    block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING, 1u);

    TEST_ASSERT_EQUAL(BOOTED, boot());
    assert_ran_the_image();
    /* On trial, and the floor not moved: only a confirm moves it. */
    assert_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE, 1u);
    TEST_ASSERT_NOT_EQUAL_UINT(0u, tfw_boot_mock_nvm_write_at());
    TEST_ASSERT_TRUE(tfw_boot_mock_nvm_write_at() < tfw_boot_mock_watchdog_at());
    TEST_ASSERT_TRUE(tfw_boot_mock_watchdog_at() < tfw_boot_mock_jump_at());
}

void test_an_unconfirmed_trial_ends_at_the_next_reset_and_never_runs_again(void)
{
    install(2u, GOOD_SIG);
    block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING, 1u);
    TEST_ASSERT_EQUAL(BOOTED, boot());

    TEST_ASSERT_EQUAL(RECOVERED, boot());
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, recovered);
    assert_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED, 1u);

    /* No second trial: every later reset is recovery, another OTA round. */
    TEST_ASSERT_EQUAL(RECOVERED, boot());
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, recovered);
    assert_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED, 1u);
}

void test_a_confirmed_image_becomes_current_and_moves_the_floor_once(void)
{
    install(3u, GOOD_SIG);
    block(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE, 1u);

    TEST_ASSERT_EQUAL(BOOTED, boot());
    assert_ran_the_image();
    assert_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 3u);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_watchdog_at());

    TEST_ASSERT_EQUAL(BOOTED, boot());
    assert_ran_the_image();
    assert_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 3u);
}

void test_an_image_under_the_floor_is_refused(void)
{
    install(4u, GOOD_SIG);
    block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 5u);

    TEST_ASSERT_EQUAL(RECOVERED, boot());
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, recovered);
    assert_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 5u);
}

void test_a_pending_image_under_the_floor_gets_no_trial(void)
{
    install(4u, GOOD_SIG);
    block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING, 5u);

    TEST_ASSERT_EQUAL(RECOVERED, boot());
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, recovered);
    assert_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED, 5u);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_boot_mock_watchdog_at());
}

void test_a_confirm_under_the_floor_never_moves_it_down(void)
{
    install(4u, GOOD_SIG);
    block(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE, 5u);

    TEST_ASSERT_EQUAL(RECOVERED, boot());
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, recovered);
    assert_block(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE, 5u);
}

void test_a_pending_image_that_does_not_verify_gets_no_trial_even_once_it_would(void)
{
    install(2u, BAD_SIG);
    block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING, 1u);

    TEST_ASSERT_EQUAL(RECOVERED, boot());
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, recovered);
    assert_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED, 1u);

    /* A failure that clears does not earn the image its trial. */
    install(2u, GOOD_SIG);
    TEST_ASSERT_EQUAL(RECOVERED, boot());
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, recovered);
}

/*
 * Power lost before each write of a round: the reset after it finds a block
 * that loads with the key still pinned, and boots only an image that verifies
 * against the floor it holds -- or serves recovery, which can take another.
 */
static void assert_safe_after_a_cut(uint8_t boot_state, uint8_t ota_state, uint32_t version,
                                    uint32_t floor)
{
    for (unsigned ok = 0u; ok < 3u; ok++) {
        tfw_boot_platform_t p;
        int outcome;

        setUp();
        install(version, GOOD_SIG);
        block(boot_state, ota_state, floor);
        tfw_boot_mock_tear_nvm(ok, 0xFFu, 0u);
        (void)boot();
        tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);

        outcome = boot();
        p = loaded();
        TEST_ASSERT_TRUE(p.rollback_floor >= floor);
        TEST_ASSERT_TRUE(p.rollback_floor <= version || p.rollback_floor == floor);
        if (outcome == BOOTED) {
            assert_ran_the_image();
            TEST_ASSERT_TRUE(version >= p.rollback_floor);
        }
    }
}

void test_a_reset_before_any_write_of_a_round_leaves_a_device_that_boots(void)
{
    assert_safe_after_a_cut(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING, 2u, 1u);
    assert_safe_after_a_cut(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE, 2u, 1u);
    assert_safe_after_a_cut(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE, 2u, 1u);
}

void test_a_cut_before_the_arm_tries_the_image_again(void)
{
    install(2u, GOOD_SIG);
    block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING, 1u);
    tfw_boot_mock_tear_nvm(0u, 0xFFu, 0u);
    (void)boot();
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    assert_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING, 1u);

    TEST_ASSERT_EQUAL(BOOTED, boot());
    assert_block(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE, 1u);
}

void test_a_cut_before_the_settle_settles_at_the_next_reset(void)
{
    install(3u, GOOD_SIG);
    block(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE, 1u);
    tfw_boot_mock_tear_nvm(0u, 0xFFu, 0u);
    (void)boot();
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    assert_block(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE, 1u);

    TEST_ASSERT_EQUAL(BOOTED, boot());
    assert_block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 3u);
}

/* The limit architecture.md names: a block torn part way fails its CRC, and
 * the next reset has no pinned key -- recovery, never a jump, and the torn
 * block is not written over. */
void test_a_block_torn_mid_write_is_recovery_and_is_not_written_over(void)
{
    uint8_t torn[TFW_BOOT_EE_LEN];

    install(2u, GOOD_SIG);
    block(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING, 1u);
    tfw_boot_mock_tear_nvm(0u, 1u, 0x00u);
    (void)boot();
    tfw_boot_mock_tear_nvm(UINT_MAX, 0u, 0u);
    memcpy(torn, tfw_boot_mock_nvm() + TFW_BOOT_EE_BASE, sizeof torn);

    TEST_ASSERT_EQUAL(RECOVERED, boot());
    TEST_ASSERT_NOT_EQUAL(TFW_OK, recovered);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(torn, tfw_boot_mock_nvm() + TFW_BOOT_EE_BASE,
                                  TFW_BOOT_EE_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_good_image_boots_and_writes_no_block);
    RUN_TEST(test_a_new_image_boots_on_trial_armed_before_the_watchdog_and_the_jump);
    RUN_TEST(test_an_unconfirmed_trial_ends_at_the_next_reset_and_never_runs_again);
    RUN_TEST(test_a_confirmed_image_becomes_current_and_moves_the_floor_once);
    RUN_TEST(test_an_image_under_the_floor_is_refused);
    RUN_TEST(test_a_pending_image_under_the_floor_gets_no_trial);
    RUN_TEST(test_a_confirm_under_the_floor_never_moves_it_down);
    RUN_TEST(test_a_pending_image_that_does_not_verify_gets_no_trial_even_once_it_would);
    RUN_TEST(test_a_reset_before_any_write_of_a_round_leaves_a_device_that_boots);
    RUN_TEST(test_a_cut_before_the_arm_tries_the_image_again);
    RUN_TEST(test_a_cut_before_the_settle_settles_at_the_next_reset);
    RUN_TEST(test_a_block_torn_mid_write_is_recovery_and_is_not_written_over);
    return UNITY_END();
}
