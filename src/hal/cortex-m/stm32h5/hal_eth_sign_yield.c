/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ETH_MAC image's tfw_sign_yield (src/crypto/deviceid_p256.h). A TCP peer
 * may ask for ATTEST, whose software P-256 sign holds the main loop for tens
 * of seconds; the signer calls this after every ladder step, so an output
 * armed over serial (tfw_sched) is still released on time.
 *
 * Deadline service only: no wire frame is read or dispatched and no lwIP
 * timer runs here, so nothing can reach the signer again, and the command in
 * progress has the transport to itself until it returns.
 */
#include "../../../caps/sched.h"
#include "../../../crypto/deviceid_p256.h"

void tfw_sign_yield(void)
{
    tfw_sched_poll();
}
