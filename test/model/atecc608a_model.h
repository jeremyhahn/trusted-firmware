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

#endif /* AG_ATECC_MODEL_H */
