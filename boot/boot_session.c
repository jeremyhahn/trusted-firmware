/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "boot_session.h"

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

ag_status_t ag_boot_session_handle(ag_ota_t *ota,
                                   uint8_t cmd,
                                   const uint8_t *payload, uint8_t plen,
                                   uint8_t *resp, uint8_t *rlen,
                                   ag_boot_session_action_t *action)
{
    *rlen   = 0u;
    *action = AG_BOOT_SESSION_CONTINUE;

    switch (cmd) {

    case AG_CMD_PING:
        /* Kept because it is the only way a host can tell "in the bootloader,
         * waiting" from "dead". Both look identical otherwise: neither answers
         * any application command. */
        for (uint8_t i = 0u; i < plen; i++) {
            resp[i] = payload[i];
        }
        *rlen = plen;
        return AG_OK;

    case AG_CMD_OTA_BEGIN:
        if (plen != 4u) {
            return AG_ERR_BAD_ARGC;
        }
        return ag_ota_begin(ota, rd_u32(payload));

    case AG_CMD_OTA_DATA: {
        /* [offset:4][data:1..] -- the offset is in every chunk on purpose; see
         * ag_ota_data for why a stream without one loses its place silently. */
        if (plen < 5u) {
            return AG_ERR_BAD_ARGC;
        }
        return ag_ota_data(ota, rd_u32(payload), &payload[4],
                           (uint8_t)(plen - 4u));
    }

    case AG_CMD_OTA_END: {
        ag_status_t st;
        if (plen != 0u) {
            return AG_ERR_BAD_ARGC;
        }
        st = ag_ota_end(ota);
        if (st == AG_OK) {
            /* Every byte arrived. NOTHING has been verified yet, and the action
             * says so: the loop measures and checks the signature, and only
             * that decides whether the image is committed. A handler that
             * reported success here would be reporting that an unverified image
             * had been accepted. */
            *action = AG_BOOT_SESSION_COMMIT;
        }
        return st;
    }

    case AG_CMD_OTA_STATUS:
        if (plen != 0u) {
            return AG_ERR_BAD_ARGC;
        }
        resp[0] = ag_ota_state(ota);
        wr_u32(&resp[1], ag_ota_received(ota));
        *rlen = 5u;
        return AG_OK;

    case AG_CMD_RESET:
        if (plen != 1u) {
            return AG_ERR_BAD_ARGC;
        }
        *action = AG_BOOT_SESSION_RESET;
        return AG_OK;

    default:
        /* Every application command lands here, which is correct and is the
         * behaviour a host needs: AG_ERR_UNKNOWN_CMD for GPIO means "this is a
         * bootloader", and the host stops trying to drive pins on a device that
         * has no application. */
        return AG_ERR_UNKNOWN_CMD;
    }
}
