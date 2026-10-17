/*
 * OTA_AUTH (#137): holds the row key's authorization for the next command.
 * There is no session and no nonce: the signature names its action, the image
 * and its version, and tfw-boot checks it against the image key and the floor.
 * Holding it for one command only keeps a stale signature from riding on a
 * later OTA_ACTIVATE or OTA_CONFIRM; any command in between drops it.
 */
#include <string.h>
#include "tfw/protocol.h"
#include "dispatch.h"
#include "ota_auth.h"

static uint8_t held[TFW_OTA_AUTH_SIG_LEN];
static uint8_t holding;

tfw_status_t tfw_ota_auth_cmd(tfw_reader_t *r, tfw_writer_t *w)
{
    (void)w;
    /* The table admits 0..64 bytes so that a short request still reaches here
     * and drops what an earlier OTA_AUTH left. */
    holding = 0u;
    if (tfw_reader_remaining(r) != TFW_OTA_AUTH_SIG_LEN) {
        return TFW_ERR_BAD_ARGC;
    }
    memcpy(held, tfw_read_blob(r, (uint8_t)TFW_OTA_AUTH_SIG_LEN), TFW_OTA_AUTH_SIG_LEN);
    holding = 1u;
    return TFW_OK;
}

const uint8_t *tfw_ota_auth_take(void)
{
    uint8_t ok = (uint8_t)(holding != 0u && tfw_dispatch_last() == TFW_CMD_OTA_AUTH);

    holding = 0u;
    return ok ? held : NULL;
}
