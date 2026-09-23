/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/* Provisioning command handlers. Present only when AG_FEATURE_PROVISION is 1;
 * see provision.c for why the default is 0 and why that default is the
 * security property rather than a convenience. */
#ifndef AG_PROVISION_H
#define AG_PROVISION_H

#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"

ag_status_t ag_prov_cmd_info(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_write_config(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_lock(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_genkey(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_pubkey(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_prov_cmd_seed_uds(ag_reader_t *r, ag_writer_t *w);

#endif /* AG_PROVISION_H */
