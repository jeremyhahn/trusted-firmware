/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Stand-in for ESP-IDF's rom/hmac.h, for test_boot_l0_esp32_mutate.c: the
 * ROM's HMAC peripheral calls, with the ROM's key block numbering. */
#ifndef TFW_TEST_L0_ROM_HMAC_H
#define TFW_TEST_L0_ROM_HMAC_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ETS_EFUSE_BLOCK0     = 0,
    ETS_EFUSE_BLOCK_KEY0 = 4,
    ETS_EFUSE_BLOCK_KEY5 = 9
} ets_efuse_block_t;

void ets_hmac_enable(void);
void ets_hmac_disable(void);
int  ets_hmac_calculate_message(ets_efuse_block_t key_block, const void *message, size_t message_len,
                                uint8_t *hmac);

#endif
