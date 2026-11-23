/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Stand-in for TF-M's tfm_builtin_key_ids.h (interface/include/crypto_keys,
 * v2.3.1; a platform may ship its own), only the HUK's id, for
 * test_tfw_dice_glue (#608).
 */
#ifndef TFW_STUB_TFM_BUILTIN_KEY_IDS_H
#define TFW_STUB_TFM_BUILTIN_KEY_IDS_H

enum tfm_builtin_key_id_t {
    TFM_BUILTIN_KEY_ID_MIN = 0x7FFF815Bu,
    TFM_BUILTIN_KEY_ID_HUK,
};

#endif
