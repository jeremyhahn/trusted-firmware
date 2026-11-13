/*
 * The image slot's TEST key, on tfwsim-manifest's object list alone (see
 * image_key.h): the key OTA_ACTIVATE's manifest gate verifies against, which
 * PROV_* writes on a device.
 */
#include <stdint.h>

#include "image_key.h"
#include "atecc.h"
#include "atecc608a_model.h"

/* PUBLIC ON PURPOSE: the private key is SHA-256 of these bytes
 * (tfw_model_p256_key), so nothing may ever trust an image it verifies.
 * sdk/go/cmd/tfwctl/e2e_ota_manifest_test.go derives the same key from the
 * same string to sign its manifests. */
static const char kImageKeySeed[] = "tfwsim-atecc608a TEST image key v1";

void tfwsim_image_key_provision(void)
{
    uint8_t image_priv[32], image_pub[64];

    tfw_model_p256_key(kImageKeySeed, image_priv, image_pub);
    tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, image_pub);
}
