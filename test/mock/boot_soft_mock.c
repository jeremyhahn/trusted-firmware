/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A soft tfw-boot, for tests (#173). What boot/tier/cdi_soft.c and
 * boot/boot_main.c leave behind for a UDS and a measurement: a version-4
 * handoff with CDI = HMAC-SHA256(UDS, measurement) and device_seed =
 * HMAC-SHA256(UDS, TFW_DEVICEID_LABEL), in the mock's handoff block, parsed by
 * the application's src/core/handoff.c. The UDS stays here: the application's
 * own UDS source is cleared, as uds_flash.c answers NULL on a device whose
 * boot section is locked, so a test that forgets the seed fails rather than
 * signing with a UDS the application never sees.
 */

#include <string.h>
#include "boot_soft_mock.h"
#include "tfw/boot.h"
#include "tfw/crypto.h"
#include "boot_measure.h"
#include "deviceid_p256.h"
#include "handoff.h"
#include "hal_mock.h"
#include "sha256.h"
#include "uds_mock.h"

void tfw_mock_boot_soft_block(const uint8_t uds[32], const uint8_t measurement[32])
{
    uint8_t  cdi[TFW_BOOT_MEASURE_LEN];
    uint8_t  seed[TFW_BOOT_SEED_LEN];
    uint8_t *block = tfw_mock_handoff();

    tfw_hmac_sha256(uds, 32u, measurement, TFW_BOOT_MEASURE_LEN, cdi);
    tfw_hmac_sha256(uds, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                    TFW_DEVICEID_LABEL_LEN, seed);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, measurement, 1u, 0u);
    tfw_boot_handoff_set_seed(block, seed);
    memset(cdi, 0, sizeof cdi);
    memset(seed, 0, sizeof seed);
    tfw_mock_set_uds(0);
}

void tfw_mock_boot_soft(const uint8_t uds[32], const uint8_t measurement[32])
{
    tfw_mock_boot_soft_block(uds, measurement);
    tfw_boot_handoff_init();
}
