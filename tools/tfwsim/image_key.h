/*
 * The element tier's image-slot key (#52), selected at link time as
 * mk/firmware.mk selects the OTA manifest gate: tools/tfwsim/image_key.c, on
 * tfwsim-manifest's object list alone, seeds the image slot with a TEST key;
 * tools/tfwsim/image_key_none.c, on every other element variant's, leaves the
 * slot as tfw_model_provision() left it. No define chooses between them.
 */
#ifndef TFWSIM_IMAGE_KEY_H
#define TFWSIM_IMAGE_KEY_H

/* Called by tfwsim_tier_seed() after tfw_model_provision() under
 * --provision-element. */
void tfwsim_image_key_provision(void);

#endif /* TFWSIM_IMAGE_KEY_H */
