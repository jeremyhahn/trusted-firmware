/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Provisioning command handlers. Present only when TFW_FEATURE_PROVISION is 1;
 * see provision.c for why the default is 0 and why that default is the
 * security property rather than a convenience. */
#ifndef TFW_PROVISION_H
#define TFW_PROVISION_H

#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

tfw_status_t tfw_prov_cmd_info(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_write_config(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_lock(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_genkey(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_pubkey(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_seed_uds(tfw_reader_t *r, tfw_writer_t *w);

#endif /* TFW_PROVISION_H */
