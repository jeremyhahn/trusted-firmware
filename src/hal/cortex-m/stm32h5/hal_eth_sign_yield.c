/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The ETH_MAC image's tfw_sign_yield (src/crypto/deviceid_p256.h). A TCP peer
 * may ask for ATTEST, whose software P-256 sign holds the main loop for tens
 * of seconds; the signer calls this after every ladder step, so an output
 * armed over serial (tfw_sched) is still released on time.
 *
 * The network is serviced but no command is dispatched: received frames go
 * into lwIP (so a healthy peer's keepalive answers are seen) and the lwIP
 * timers and lease expiry run; the wire's receive callback only buffers, and
 * neither the dial, the say flush nor the link poll runs here, so nothing can
 * reach the signer again, and the command in progress has the transport to
 * itself until it returns. Buffered requests are dispatched after it does.
 */
#include "../../../caps/sched.h"
#include "../../../crypto/deviceid_p256.h"
#include "hal_eth_wire.h"

void tfw_sign_yield(void)
{
    tfw_sched_poll();
    tfw_h5_eth_service_rx_nodispatch(); /* keepalive answers in; data only buffered */
    tfw_h5_eth_service_deadlines();     /* lwIP timers, lease expiry; no dispatch */
}
