/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The software tier's side of tfwsim. --uds plays a soft tfw-boot holding that
 * UDS (test/mock/boot_soft_mock.c): the application gets the CDI and the
 * DeviceID seed in the handoff and never the UDS (#173), so the soft root
 * derives its P-256 DeviceID from the seed and signs ES256 under it (#48,
 * #130): the end-to-end tests in sdk/go need a device that has an identity
 * without an element. Unset, nothing is handed off and the device is
 * unprovisioned and says so.
 *
 * An option it does not offer is an error in main.c rather than silently
 * ignored -- `tfwsim --eeprom x` must not quietly start a device that never
 * saw the file.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tier.h"
#include "boot_soft_mock.h"
#include "uds_mock.h"

static uint8_t uds[32];
static uint8_t have_uds;

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

int tfwsim_tier_option(int argc, char **argv)
{
    size_t i;

    if (strcmp(argv[0], "--uds") != 0) {
        return 0;
    }
    if (argc < 2 || strlen(argv[1]) != 2u * sizeof uds) {
        fprintf(stderr, "tfwsim: --uds: want 64 hex characters (32 bytes)\n");
        return -1;
    }
    for (i = 0; i < sizeof uds; i++) {
        int hi = hex_nibble(argv[1][2u * i]);
        int lo = hex_nibble(argv[1][2u * i + 1u]);

        if (hi < 0 || lo < 0) {
            fprintf(stderr, "tfwsim: --uds: want 64 hex characters (32 bytes)\n");
            return -1;
        }
        uds[i] = (uint8_t)((hi << 4) | lo);
    }
    have_uds = 1u;
    return 2;
}

const char *tfwsim_tier_usage(void)
{
    return " [--uds HEX64]";
}

/* What a soft tfw-boot leaves for the application (#173): a version-4 handoff
 * with the CDI and the DeviceID seed, both derived from the UDS, which stays
 * here. The measurement is zero, as the FWHash of a build with no handoff
 * always was. With no --uds nothing is handed off and the device is
 * unprovisioned. */
void tfwsim_tier_seed(void)
{
    static const uint8_t measurement[32];

    tfw_mock_set_uds(NULL);
    if (have_uds) {
        tfw_mock_boot_soft_block(uds, measurement);
    }
}
