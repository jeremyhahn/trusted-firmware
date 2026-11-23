/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The non-secure side's first call into the DICE partition (#608 slice 3):
 * it asks for the DeviceID public key through the NS client
 * (src/tz/tz_s_client.c over psa_call) and prints one line,
 * "tfw-dice: deviceid ok" or "tfw-dice: status <n>", which
 * test/integration/cortex-m/qemu_tfm_test.sh looks for.
 *
 * It rides in tf-m-extras' example non-secure application without a change
 * to that application's source: scripts/tfm-build.sh adds this file and the
 * client to its tfm_ns target and links it with -Wl,--wrap=psa_framework_version,
 * so the application's psa_framework_version() call, made after
 * stdio_init() and its "Non-Secure system starting..." line, comes here
 * first and then goes on to the real one.
 */
#include <stdint.h>
#include <stdio.h>
#include "psa/client.h"
#include "tfw/tz_s_api.h"

uint32_t __real_psa_framework_version(void);
uint32_t __wrap_psa_framework_version(void);

/* An all-zero key is no key: the partition never answers one. */
static int key_is_set(const uint8_t pub[64])
{
    uint8_t acc = 0;
    for (size_t i = 0; i < 64; i++) {
        acc |= pub[i];
    }
    return acc != 0;
}

static void tfw_dice_ns_check(void)
{
    uint8_t pub[64] = {0};
    int st = (int)tfw_s_dice_deviceid_pub(pub);

    if (st == (int)TFW_OK && key_is_set(pub)) {
        printf("tfw-dice: deviceid ok\r\n");
    } else if (st == (int)TFW_OK) {
        printf("tfw-dice: status ok, empty key\r\n");
    } else {
        printf("tfw-dice: status %d\r\n", st);
    }
}

uint32_t __wrap_psa_framework_version(void)
{
    static int done;

    if (!done) {
        done = 1;
        tfw_dice_ns_check();
    }
    return __real_psa_framework_version();
}
