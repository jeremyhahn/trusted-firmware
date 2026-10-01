/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>

#include "handoff.h"
#include "ag/hal.h"
#include "boot_measure.h"
#include "secure_zero.h"

static uint8_t have;
static uint8_t tier;
static uint8_t cdi[AG_BOOT_MEASURE_LEN];
static uint8_t have_cdi;
static uint8_t measurement[AG_BOOT_MEASURE_LEN];
static uint8_t reset_cause;

void ag_boot_handoff_init(void)
{
    uint8_t    *block = ag_hal_handoff_block();
    uint32_t    image_version;
    ag_status_t st;

    have     = 0u;
    have_cdi = 0u;

    st = ag_boot_handoff_parse(block, &tier, cdi, measurement,
                               &image_version, &reset_cause);
    if (st == AG_OK) {
        uint8_t i, cdi_is_zero = 1u;

        have = 1u;

        /*
         * A valid block with an all-zero CDI is ag-boot's OWN "unprovisioned"
         * signal -- boot/boot_main.c hands one off verbatim rather than
         * refusing to boot, precisely so the application can see an
         * unprovisioned device for what it is. A crypto provider that treated
         * this as a usable key would produce evidence that verifies against
         * the one key every unprovisioned build shares -- the exact failure
         * ag_crypto_soft_uds's own placeholder-refusal exists to prevent one
         * layer down. So it is not offered as a CDI at all: `have_cdi` stays
         * 0 and `ag_boot_handoff_take_cdi` reports nothing to take.
         *
         * This is NOT the same as "no valid handoff" -- `have` is still 1,
         * and a provider naming this tier must stay unprovisioned rather
         * than falling back to a locally derived identity, or its signature
         * would not correspond to the real measurement this same handoff
         * hands FWHash. See src/crypto/soft/provider.c's own comment on the
         * point and docs/dice-attestation/architecture.md.
         */
        for (i = 0u; i < sizeof cdi; i++) {
            if (cdi[i] != 0u) { cdi_is_zero = 0u; }
        }
        have_cdi = (uint8_t)(cdi_is_zero ? 0u : 1u);
    } else {
        ag_secure_zero(cdi, sizeof cdi);
        memset(measurement, 0, sizeof measurement);
        tier        = 0u;
        reset_cause = 0u;
    }

    /* Consumed. Whatever the parse decided, the raw block does not outlive
     * this call -- a stack that later grows this far finds zero, not a CDI. */
    ag_secure_zero(block, AG_BOOT_HANDOFF_LEN);
}

const uint8_t *ag_boot_handoff_measurement(void)
{
    return have ? measurement : 0;
}

uint8_t ag_boot_handoff_reset_cause(uint8_t *out)
{
    if (!have) {
        return 0u;
    }
    *out = reset_cause;
    return 1u;
}

const uint8_t *ag_boot_handoff_tier(void)
{
    return have ? &tier : 0;
}

uint8_t ag_boot_handoff_take_cdi(uint8_t out[AG_BOOT_MEASURE_LEN])
{
    if (!have_cdi) {
        return 0u;
    }
    memcpy(out, cdi, sizeof cdi);
    ag_secure_zero(cdi, sizeof cdi);
    have_cdi = 0u;
    return 1u;
}
