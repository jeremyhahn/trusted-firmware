#ifndef TFW_OTA_AUTH_H
#define TFW_OTA_AUTH_H

#include <stdint.h>
#include "tfw/boot.h"
#include "tfw/status.h"
#include "reader.h"
#include "writer.h"

/*
 * OTA_AUTH (#137, CA m502 item 2): the row key's authorization for the next
 * OTA_ACTIVATE or OTA_CONFIRM, a COSE_Sign1 ES256 signature, r||s
 * (docs/ota/api.md, "Authorization by the row key"). The application does not
 * verify it; it holds it for the command right after OTA_AUTH, which stores it
 * where tfw-boot verifies it at reset. Linked with src/core/ota_activate.c.
 */

/* [sig:64]. Any other length drops what is held and answers TFW_ERR_BAD_ARGC. */
tfw_status_t tfw_ota_auth_cmd(tfw_reader_t *r, tfw_writer_t *w);

/* The held signature, when OTA_AUTH was the command just before this one;
 * NULL otherwise. Either way nothing is held afterwards. */
const uint8_t *tfw_ota_auth_take(void);

/* Whether an OTA_AUTH, of any length or signature, has arrived since the last
 * tfw_ota_auth_settle. While an activation is pending, an OTA_ACTIVATE is the
 * retry of that exchange only when none has (m1117 (1)). */
uint8_t tfw_ota_auth_arrived(void);

/* Called by the OTA_ACTIVATE whose activation becomes pending. */
void tfw_ota_auth_settle(void);

#endif
