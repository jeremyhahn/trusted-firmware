/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Provisioning command handlers. Present only when TFW_FEATURE_PROVISION is 1;
 * see provision.c for why the default is 0 and why that default is the
 * security property rather than a convenience. */
#ifndef TFW_PROVISION_H
#define TFW_PROVISION_H

#include <stdint.h>
#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

tfw_status_t tfw_prov_cmd_info(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_write_config(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_lock(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_genkey(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_pubkey(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_seed_uds(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_write_pubkey(tfw_reader_t *r, tfw_writer_t *w);

/*
 * The backend under those handlers: one per identity root, chosen by the
 * object list (mk/firmware.mk PROV_SRCS), never by a conditional. The
 * secure element's is provision_atecc.c; the eFuse UDS's is provision_uds.c.
 * The handlers own the wire -- argument parsing and the reply -- and a
 * backend owns every refusal that depends on the part. A backend that does
 * not have a command answers TFW_ERR_UNSUPPORTED.
 */
tfw_status_t tfw_prov_info(uint8_t rev[4], uint8_t cfg[32], uint8_t lock[2]);
tfw_status_t tfw_prov_write_config(uint8_t block, const uint8_t data[32]);
tfw_status_t tfw_prov_lock(uint8_t zone, uint16_t crc);
tfw_status_t tfw_prov_genkey(uint8_t slot, uint8_t pub[64]);
tfw_status_t tfw_prov_pubkey(uint8_t slot, uint8_t pub[64]);
tfw_status_t tfw_prov_seed_uds(uint8_t slot);
/* Writes one coordinate of a P-256 public key (half 0 = X, 1 = Y) into a data
 * slot, then reads the slot's whole X||Y back into back. */
tfw_status_t tfw_prov_write_pubkey(uint8_t slot, uint8_t half, const uint8_t coord[32],
                                   uint8_t back[64]);

#endif /* TFW_PROVISION_H */
