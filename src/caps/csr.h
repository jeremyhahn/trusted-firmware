/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_CSR_H
#define TFW_CAPS_CSR_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/crypto.h"
#include "alias.h"
#include "../core/reader.h"
#include "../core/writer.h"

/* The CertificationRequestInfo, whole: its fixed width is what lets a host
 * rebuild it byte for byte from the same four values. */
#define TFW_CSR_CRI_LEN 365u

/*
 * SHA-256 over the CertificationRequestInfo: the bytes the DeviceID key signs.
 * p->nonce is b, the host's 32 bytes; p->sig is not read.
 */
tfw_status_t tfw_csr_cri_digest(const tfw_alias_parts_t *p,
                              uint8_t out[TFW_SHA256_LEN]);

/*
 * Emits the CertificationRequestInfo and copies the window [off, off+cap) of it
 * into out, with the contract tfw_alias_cert has: *total is the whole length,
 * *got what the window yielded, and an offset past the end is TFW_ERR_RANGE.
 * The command never sends these bytes -- the host rebuilds them -- so this is
 * the seam that lets the vectors pin what the device signs.
 */
tfw_status_t tfw_csr_cri(const tfw_alias_parts_t *p, uint16_t off,
                       uint8_t *out, uint8_t cap,
                       uint16_t *total, uint8_t *got);

tfw_status_t tfw_csr_cmd(tfw_reader_t *r, tfw_writer_t *w);

#endif /* TFW_CAPS_CSR_H */
