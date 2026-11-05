/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * What test/mock/gp/tee_crypto_mock.c saw: the AAD of the last AEAD
 * operation, and the operations, objects and TEE_Malloc buffers still
 * allocated. A test sets tfw_tee_mock_on_decrypt to act as the normal world
 * at the start of TEE_AEDecryptFinal.
 */
#ifndef TFW_TEE_CRYPTO_MOCK_H
#define TFW_TEE_CRYPTO_MOCK_H

#include <stddef.h>
#include <stdint.h>

#define TFW_TEE_MOCK_AAD_MAX 256u

extern uint8_t tfw_tee_mock_aad[TFW_TEE_MOCK_AAD_MAX];
extern size_t tfw_tee_mock_aad_len;
extern int tfw_tee_mock_live;
extern void (*tfw_tee_mock_on_decrypt)(void);

void tfw_tee_crypto_mock_reset(void);

#endif /* TFW_TEE_CRYPTO_MOCK_H */
