/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A part with one-time UDS key storage, linked in place of
 * src/hal/esp32/prov_uds.c. It keeps the burned key where only the test can
 * see it and computes the HMAC under it in software, as the part's
 * peripheral would; src/caps/provision_uds.c cannot tell the two apart.
 */

#include <string.h>
#include "prov_uds_mock.h"
#include "caps/provision_uds.h"
#include "crypto/sha256.h"

static uint8_t      fill_base;
static uint8_t      key[32];
static uint8_t      have_key;
static unsigned     draws;
static unsigned     burns;
static tfw_status_t random_st;
static tfw_status_t burn_st;
static tfw_status_t hmac_st;

void tfw_mock_prov_uds_reset(uint8_t fill)
{
    fill_base = fill;
    memset(key, 0, sizeof key);
    have_key  = 0u;
    draws     = 0u;
    burns     = 0u;
    random_st = TFW_OK;
    burn_st   = TFW_OK;
    hmac_st   = TFW_OK;
}

void tfw_mock_prov_uds_fail_random(tfw_status_t st) { random_st = st; }
void tfw_mock_prov_uds_fail_burn(tfw_status_t st)   { burn_st = st; }
void tfw_mock_prov_uds_fail_hmac(tfw_status_t st)   { hmac_st = st; }

const uint8_t *tfw_mock_prov_uds_burned(void) { return have_key ? key : 0; }
unsigned       tfw_mock_prov_uds_draws(void)  { return draws; }
unsigned       tfw_mock_prov_uds_burns(void)  { return burns; }

int tfw_prov_uds_present(void)
{
    return have_key ? 1 : 0;
}

tfw_status_t tfw_prov_uds_random(uint8_t uds[32])
{
    unsigned i;

    draws++;
    if (random_st != TFW_OK) {
        return random_st;
    }
    for (i = 0u; i < 32u; i++) {
        uds[i] = (uint8_t)(fill_base + i);
    }
    return TFW_OK;
}

tfw_status_t tfw_prov_uds_burn(const uint8_t uds[32])
{
    burns++;
    if (burn_st != TFW_OK) {
        return burn_st;
    }
    if (have_key) {
        /* A real part has no second block for the same purpose to give; the
         * backend must never get this far. */
        return TFW_ERR_INTERNAL;
    }
    memcpy(key, uds, sizeof key);
    have_key = 1u;
    return TFW_OK;
}

tfw_status_t tfw_prov_uds_hmac(const uint8_t *msg, size_t len, uint8_t out[32])
{
    if (hmac_st != TFW_OK || !have_key) {
        return hmac_st != TFW_OK ? hmac_st : TFW_ERR_CRYPTO;
    }
    tfw_hmac_sha256(key, (uint8_t)sizeof key, msg, (uint16_t)len, out);
    return TFW_OK;
}
