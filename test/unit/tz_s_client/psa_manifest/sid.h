/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for the psa_manifest/sid.h TF-M generates from
 * src/tz/tfm/tfw_dice.yaml: the service's SID, version and stateless handle.
 * The handle's value is TF-M's to assign ("auto"); any value does here.
 */
#ifndef TFW_TEST_PSA_MANIFEST_SID_H
#define TFW_TEST_PSA_MANIFEST_SID_H

#define TFW_DICE_SERVICE_SID     (0xD1CE0000U)
#define TFW_DICE_SERVICE_VERSION (1U)
#define TFW_DICE_SERVICE_HANDLE  (0x40000111U)

#endif
