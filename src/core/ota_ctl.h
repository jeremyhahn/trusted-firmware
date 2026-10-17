/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_OTA_CTL_H
#define TFW_OTA_CTL_H

#include "tfw/status.h"
#include "reader.h"
#include "writer.h"

/* ADR decision 8's OTA_INFO (0x6C), OTA_ACTIVATE (0x6D) and OTA_CONFIRM (0x6E)
 * -- issue #77. Linked only with TFW_FEATURE_OTA_CTL; see ota_ctl.c. */
tfw_status_t tfw_otactl_cmd_info(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_otactl_cmd_confirm(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_otactl_cmd_activate(tfw_reader_t *r, tfw_writer_t *w);

#endif
