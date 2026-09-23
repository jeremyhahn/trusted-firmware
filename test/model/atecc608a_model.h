/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Controls for the software ATECC608A. See atecc608a.c for what it does and
 * does not model, and why having one is approved rather than assumed. */
#ifndef AG_ATECC_MODEL_H
#define AG_ATECC_MODEL_H

#include <stdint.h>

void     ag_model_reset(void);
/* Gives the element a UDS and an ECC key in slot 0 -- what agprov does. */
void     ag_model_provision(void);
/* NACK the next n reads, as the real part does while a command executes. */
void     ag_model_set_busy(uint8_t reads);
/* Make every command fail with this element status code. */
void     ag_model_force_status(uint8_t status);
void     ag_model_set_counter(uint32_t v);
uint8_t  ag_model_last_opcode(void);
uint8_t  ag_model_is_awake(void);
uint8_t *ag_model_tempkey(void);
uint8_t  ag_model_config_locked(void);
uint8_t  ag_model_data_locked(void);
/* The slot's contents, so a test can assert that what landed is what the
 * ELEMENT produced rather than anything the host sent. Returns NULL for a slot
 * the part does not have. */
const uint8_t *ag_model_slot(uint8_t n);

/* Whether the element's Verify answers "valid". The model does no curve
 * arithmetic: a test asserting that ECDSA works would be testing the model. */
void           ag_model_set_verify_result(uint8_t pass);
/* No element on the board: every transfer NACKs. Distinct from a busy part --
 * a driver must tell "not there" from "not yet". */
void           ag_model_set_absent(uint8_t yes);
uint8_t        ag_model_verify_seen(void);
/* The signature the driver actually sent, so a test can prove it forwarded the
 * one it was given rather than something it constructed. */
const uint8_t *ag_model_verify_last_sig(void);

#endif /* AG_ATECC_MODEL_H */
