/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The element tier with --image-key HEX128, for tfwsim-manifest alone (#52).
 *
 * OTA_ACTIVATE's manifest gate verifies the SUIT ES256 with
 * tfw_crypto_verify_image, against the public key in the element's image
 * slot. On a device PROV_* writes that slot; tfwsim-manifest has no
 * provisioning commands, so the key comes on the command line.
 *
 * By FILE SELECTION, not a define: tools/ is under lint-conditionals, so
 * tier_atecc608a.c cannot grow an #if for one simulator. This file includes
 * it with its three tier entry points renamed and wraps them, and only the
 * tfwsim-manifest rule in mk/host.mk links this file in its place. Every
 * other simulator that links tier_atecc608a.c keeps main's options and usage.
 */

#include "tier.h"

int         element_tier_option(int argc, char **argv);
const char *element_tier_usage(void);
void        element_tier_seed(void);

#define tfwsim_tier_option element_tier_option
#define tfwsim_tier_usage  element_tier_usage
#define tfwsim_tier_seed   element_tier_seed
#include "tier_atecc608a.c"
#undef tfwsim_tier_option
#undef tfwsim_tier_usage
#undef tfwsim_tier_seed

#include "atecc.h"

static uint8_t have_image_key;
static uint8_t image_key[64];           /* --image-key: the image slot's X||Y */

int tfwsim_tier_option(int argc, char **argv)
{
    if (strcmp(argv[0], "--image-key") == 0) {
        if (argc < 2) { return fail(argv[0], "needs a value"); }
        if (hex_bytes(argv[1], strlen(argv[1]), image_key, sizeof image_key) < 0) {
            return fail("--image-key", "want 128 hex characters (a P-256 X||Y)");
        }
        have_image_key = 1u;
        return 2;
    }
    return element_tier_option(argc, argv);
}

const char *tfwsim_tier_usage(void)
{
    static char usage[256];

    (void)snprintf(usage, sizeof usage, "%s [--image-key HEX128]", element_tier_usage());
    return usage;
}

/* After the element tier's seed, whose first act is tfw_model_reset(). */
void tfwsim_tier_seed(void)
{
    element_tier_seed();
    if (have_image_key) {
        tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, image_key);
    }
}
