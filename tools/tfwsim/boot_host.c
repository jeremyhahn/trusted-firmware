/*
 * tools/tfwsim/boot_host.c -- the real tfw-boot in front of the soft simulator
 * (sim_boot.h, #52).
 *
 *   --boot-key FILE    the P-256 public key tfw-boot verifies with: 64 raw bytes,
 *                      X then Y, big-endian (micro-ecc's form). Turns the
 *                      bootloader on. The key is the caller's -- a test makes a
 *                      fresh one per run; no private key exists in this tree.
 *   --app-image FILE   the image the device leaves the factory with, in the form
 *                      the stager stores: the body, then its trailer. The body
 *                      is placed at the application region's base and the
 *                      trailer at the region's end, as the stage copies them.
 *   --boot-floor N     the anti-rollback floor the factory block is written
 *                      with (default 0), so a test can stand below it without
 *                      first settling a trial.
 *
 * With the key, power on is a boot pass (boot/boot_main.c, renamed by the link
 * to tfwsim_boot_main) through boot/hal/host/boot_hal_host.c, and so is every
 * tfw_hal_reset the firmware makes: the link wraps it (-Wl,--wrap) and the
 * wrapper goes back to the top of main.c's startup. The EEPROM, the handoff,
 * the flash and the external store survive; the mock's RAM-only state does not.
 * A pass that ends in recovery ends tfwsim, with the status on stderr and exit
 * status 3: recovery on this build serves no UART (BOOT_OTA=0), so there is
 * nothing more the device would say.
 *
 * Without the key, nothing here runs and a reset is the mock's count.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_boot.h"
#include "boot_host.h"
#include "boot_platform.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/hal.h"

/* boot/verify/verify_p256_soft.c's key slot, defined here in place of the
 * erased boot/verify/p256_key_slot.c, and filled from --boot-key. Left erased
 * it refuses every image, as the slot that ships does. */
uint8_t tfw_boot_p256_key[64];

int  tfwsim_boot_main(void);
void __real_tfw_hal_reset(void);
void __wrap_tfw_hal_reset(void);

#define RECOVERY_EXIT 3
/* A pass that keeps asking for a reset is a loop on the part; here, an end. */
#define RESET_PASSES  4

static uint8_t     enabled;
static const char *image_path;
static const char *floor_arg;

static int read_file(const char *path, uint8_t *buf, size_t cap, size_t *len)
{
    FILE  *f = fopen(path, "rb");
    size_t n;

    if (f == NULL) {
        perror(path);
        return -1;
    }
    n = fread(buf, 1u, cap, f);
    if (n == cap && fgetc(f) != EOF) {
        fprintf(stderr, "tfwsim: %s is longer than %zu bytes\n", path, cap);
        (void)fclose(f);
        return -1;
    }
    (void)fclose(f);
    *len = n;
    return 0;
}

int tfwsim_boot_option(int argc, char **argv)
{
    size_t n;

    if (strcmp(argv[0], "--boot-key") != 0 && strcmp(argv[0], "--app-image") != 0 &&
        strcmp(argv[0], "--boot-floor") != 0) {
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "tfwsim: %s needs an argument\n", argv[0]);
        return -1;
    }
    if (strcmp(argv[0], "--app-image") == 0) {
        image_path = argv[1];
        return 2;
    }
    if (strcmp(argv[0], "--boot-floor") == 0) {
        floor_arg = argv[1];
        return 2;
    }
    if (read_file(argv[1], tfw_boot_p256_key, sizeof tfw_boot_p256_key, &n) != 0) {
        return -1;
    }
    if (n != sizeof tfw_boot_p256_key) {
        fprintf(stderr, "tfwsim: --boot-key wants 64 bytes (X||Y), %s has %zu\n", argv[1], n);
        return -1;
    }
    enabled = 1u;
    return 2;
}

const char *tfwsim_boot_usage(void)
{
    return " [--boot-key FILE [--app-image FILE] [--boot-floor N]]";
}

/* The factory image: the body at the region's base, the trailer at its end. */
static int load_image(void)
{
    static uint8_t img[TFW_BOOT_MAX_IMAGE_LEN + TFW_BOOT_TRAILER_LEN];
    size_t         n;
    size_t         body;

    if (read_file(image_path, img, sizeof img, &n) != 0) {
        return -1;
    }
    if (n <= TFW_BOOT_TRAILER_LEN) {
        fprintf(stderr, "tfwsim: %s has no body before its trailer\n", image_path);
        return -1;
    }
    body = n - TFW_BOOT_TRAILER_LEN;
    memcpy(&tfw_boot_host_flash()[TFW_BOOT_APP_BASE], img, body);
    memcpy(&tfw_boot_host_flash()[TFW_BOOT_TRAILER_ADDR], &img[body], TFW_BOOT_TRAILER_LEN);
    return 0;
}

/* The platform block a factory writes: GOOD, nothing pending, the floor
 * --boot-floor names. Only where the EEPROM holds none -- the simulator's power
 * on is a new part -- except that a given floor is always written. */
static int provision(void)
{
    tfw_boot_platform_t p;
    uint8_t            *ee = &tfw_mock_eeprom()[TFW_BOOT_EE_BASE];
    char               *end;
    unsigned long       floor;

    if (tfw_boot_platform_parse(ee, &p) != TFW_OK) {
        memset(&p, 0, sizeof p);
        p.boot_state = TFW_BOOT_STATE_GOOD;
        p.ota_state  = TFW_BOOT_OTA_IDLE;
    }
    if (floor_arg != NULL) {
        floor = strtoul(floor_arg, &end, 0);
        if (*floor_arg == '\0' || *end != '\0' || floor > 0xFFFFFFFFuL) {
            fprintf(stderr, "tfwsim: --boot-floor wants a 32-bit number, not %s\n", floor_arg);
            return -1;
        }
        p.rollback_floor = (uint32_t)floor;
    }
    tfw_boot_platform_serialise(&p, ee);
    return 0;
}

static int pass(void)
{
    unsigned i;

    for (i = 0u; i < RESET_PASSES; i++) {
        switch (tfw_boot_host_pass(tfwsim_boot_main)) {
        case TFW_BOOT_HOST_JUMPED:
            return 0;
        case TFW_BOOT_HOST_RESET:
            tfw_boot_host_set_cause(TFW_RESET_WATCHDOG);
            continue;
        case TFW_BOOT_HOST_RECOVERY:
        default:
            fprintf(stderr, "tfwsim: tfw-boot recovery, status %u\n",
                    (unsigned)tfw_boot_host_recovery_reason());
            return RECOVERY_EXIT;
        }
    }
    fprintf(stderr, "tfwsim: tfw-boot reset %u times without a jump\n", RESET_PASSES);
    return RECOVERY_EXIT;
}

int tfwsim_boot_power_on(void)
{
    if (!enabled) {
        if (image_path != NULL) {
            fprintf(stderr, "tfwsim: --app-image needs --boot-key\n");
            return 2;
        }
        return 0;
    }
    if (image_path != NULL && load_image() != 0) {
        return 2;
    }
    if (provision() != 0) {
        return 2;
    }
    tfw_boot_host_set_cause(TFW_RESET_POWER_ON);
    return pass();
}

int tfwsim_boot_reset(void)
{
    static uint8_t ee[4096];
    uint8_t        handoff[TFW_BOOT_HANDOFF_LEN];
    size_t         ee_len = tfw_mock_eeprom_size();

    if (ee_len > sizeof ee) {
        fprintf(stderr, "tfwsim: the mock EEPROM outgrew the reset's copy\n");
        return 1;
    }
    memcpy(ee, tfw_mock_eeprom(), ee_len);
    memcpy(handoff, tfw_mock_handoff(), sizeof handoff);
    tfw_mock_reset();
    memcpy(tfw_mock_eeprom(), ee, ee_len);
    memcpy(tfw_mock_handoff(), handoff, sizeof handoff);
    /* The AVR firmware resets through the watchdog (src/hal/avr). */
    tfw_mock_set_reset_cause(TFW_RESET_WATCHDOG);
    tfw_boot_host_set_cause(TFW_RESET_WATCHDOG);
    return pass();
}

void __wrap_tfw_hal_reset(void)
{
    if (enabled) {
        tfwsim_reboot();
    }
    __real_tfw_hal_reset();
}
