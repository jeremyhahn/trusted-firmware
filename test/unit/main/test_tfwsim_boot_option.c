/*
 * test/unit/main/test_tfwsim_boot_option.c -- tfwsim's bootloader options
 * (tools/tfwsim/boot_host.c, #629).
 *
 * --boot-key wants exactly 64 bytes, --app-image needs the key, and an option
 * without its argument is refused. boot_host.c is linked alone: every symbol
 * it calls but this test does not exercise is a stand-in below, and
 * tfwsim_reboot counts and jumps back to the test (it does not return).
 *
 * boot_host.c's state is file-static and one process runs every case, so the
 * cases run in the order main() lists them: nothing turns the key on until
 * test_a_64_byte_key_turns_the_bootloader_on.
 */
#define _POSIX_C_SOURCE 200809L /* mkstemp, fdopen, unlink */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"
#include "sim_boot.h"
#include "boot_host.h"
#include "boot_platform.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/hal.h"

extern uint8_t tfw_boot_p256_key[64];
void           __wrap_tfw_hal_reset(void);

static unsigned reboots;
static unsigned real_resets;
static jmp_buf  reboot_jmp;
static char     key_path[4096];

/* ---- stand-ins for what boot_host.c calls and this test does not reach ---- */

_Noreturn void tfwsim_reboot(void)
{
    reboots++;
    longjmp(reboot_jmp, 1);
}

void __real_tfw_hal_reset(void)
{
    real_resets++;
}

int tfwsim_boot_main(void)
{
    return 0;
}

void tfw_boot_host_set_cause(tfw_reset_cause_t c)
{
    (void)c;
}

tfw_boot_host_exit_t tfw_boot_host_pass(int (*entry)(void))
{
    (void)entry;
    return TFW_BOOT_HOST_JUMPED;
}

uint8_t *tfw_boot_host_flash(void)
{
    static uint8_t flash[16];
    return flash;
}

tfw_status_t tfw_boot_host_recovery_reason(void)
{
    return TFW_OK;
}

tfw_status_t tfw_boot_platform_parse(const uint8_t *raw, tfw_boot_platform_t *out)
{
    (void)raw;
    (void)out;
    return TFW_OK;
}

void tfw_boot_platform_serialise(const tfw_boot_platform_t *in, uint8_t *raw)
{
    (void)in;
    (void)raw;
}

uint8_t *tfw_mock_eeprom(void)
{
    static uint8_t ee[16];
    return ee;
}

size_t tfw_mock_eeprom_size(void)
{
    return 0u;
}

uint8_t *tfw_mock_handoff(void)
{
    static uint8_t handoff[TFW_BOOT_HANDOFF_LEN];
    return handoff;
}

void tfw_mock_reset(void)
{
}

void tfw_mock_set_reset_cause(tfw_reset_cause_t c)
{
    (void)c;
}

/* ---- helpers ---- */

/* A fresh file under TMPDIR (or /tmp) holding bytes 0, 1, ... n-1. */
static const char *write_key(size_t n)
{
    const char *dir = getenv("TMPDIR");
    int         fd;
    FILE       *f;
    size_t      i;

    if (dir == NULL || dir[0] == '\0') {
        dir = "/tmp";
    }
    (void)snprintf(key_path, sizeof key_path, "%s/tfwsim-key-XXXXXX", dir);
    fd = mkstemp(key_path);
    TEST_ASSERT_TRUE_MESSAGE(fd >= 0, "mkstemp");
    f = fdopen(fd, "wb");
    TEST_ASSERT_NOT_NULL(f);
    for (i = 0u; i < n; i++) {
        TEST_ASSERT_EQUAL_INT((int)i, fputc((int)i, f));
    }
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
    return key_path;
}

static int option(const char *a, const char *b)
{
    /* boot_host.c keeps argv[1] for --app-image and --boot-floor: static. */
    static char opt[32];
    static char arg[sizeof key_path];
    char       *argv[3] = {opt, arg, NULL};

    (void)snprintf(opt, sizeof opt, "%s", a);
    (void)snprintf(arg, sizeof arg, "%s", b == NULL ? "" : b);
    return tfwsim_boot_option(b == NULL ? 1 : 2, argv);
}

void setUp(void)
{
    reboots     = 0u;
    real_resets = 0u;
    key_path[0] = '\0';
}

void tearDown(void)
{
    if (key_path[0] != '\0') {
        (void)unlink(key_path);
        key_path[0] = '\0';
    }
}

/* ---- cases, in order ---- */

static void test_another_option_is_left_to_main(void)
{
    TEST_ASSERT_EQUAL_INT(0, option("--port", "/dev/null"));
}

static void test_an_option_without_its_argument_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(-1, option("--boot-key", NULL));
    TEST_ASSERT_EQUAL_INT(-1, option("--app-image", NULL));
    TEST_ASSERT_EQUAL_INT(-1, option("--boot-floor", NULL));
}

static void test_a_missing_key_file_is_refused(void)
{
    char gone[sizeof key_path];

    (void)strcpy(gone, write_key(0u));
    TEST_ASSERT_EQUAL_INT(0, unlink(gone));
    key_path[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, option("--boot-key", gone));
}

static void test_a_63_byte_key_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(-1, option("--boot-key", write_key(63u)));
}

static void test_a_65_byte_key_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(-1, option("--boot-key", write_key(65u)));
}

static void test_an_image_without_the_key_is_refused_at_power_on(void)
{
    TEST_ASSERT_EQUAL_INT(2, option("--app-image", "image.bin"));
    TEST_ASSERT_EQUAL_INT(2, tfwsim_boot_power_on());
    /* No key is on, so a reset is the mock's, not a boot pass. */
    if (setjmp(reboot_jmp) == 0) {
        __wrap_tfw_hal_reset();
    }
    TEST_ASSERT_EQUAL_UINT(0u, reboots);
    TEST_ASSERT_EQUAL_UINT(1u, real_resets);
}

static void test_a_floor_is_taken(void)
{
    TEST_ASSERT_EQUAL_INT(2, option("--boot-floor", "3"));
}

static void test_a_64_byte_key_turns_the_bootloader_on(void)
{
    uint8_t want[64];
    size_t  i;

    for (i = 0u; i < sizeof want; i++) {
        want[i] = (uint8_t)i;
    }
    memset(tfw_boot_p256_key, 0xA5, sizeof tfw_boot_p256_key);
    TEST_ASSERT_EQUAL_INT(2, option("--boot-key", write_key(64u)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, tfw_boot_p256_key, sizeof want);
    if (setjmp(reboot_jmp) == 0) {
        __wrap_tfw_hal_reset();
    }
    TEST_ASSERT_EQUAL_UINT(1u, reboots);
    TEST_ASSERT_EQUAL_UINT(0u, real_resets);
}

static void test_the_usage_names_every_option(void)
{
    const char *u = tfwsim_boot_usage();

    TEST_ASSERT_NOT_NULL(strstr(u, "--boot-key"));
    TEST_ASSERT_NOT_NULL(strstr(u, "--app-image"));
    TEST_ASSERT_NOT_NULL(strstr(u, "--boot-floor"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_another_option_is_left_to_main);
    RUN_TEST(test_an_option_without_its_argument_is_refused);
    RUN_TEST(test_a_missing_key_file_is_refused);
    RUN_TEST(test_a_63_byte_key_is_refused);
    RUN_TEST(test_a_65_byte_key_is_refused);
    RUN_TEST(test_an_image_without_the_key_is_refused_at_power_on);
    RUN_TEST(test_a_floor_is_taken);
    RUN_TEST(test_a_64_byte_key_turns_the_bootloader_on);
    RUN_TEST(test_the_usage_names_every_option);
    return UNITY_END();
}
