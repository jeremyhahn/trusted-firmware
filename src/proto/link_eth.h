/*
 * The Ethernet allow-list: the commands a network peer may send. One list for
 * every network link (link_eth.c's W5x00 socket, the STM32H5's TCP listener in
 * hal_eth_wire.c); device control and provisioning stay on serial (SPEC, the
 * STM32H5 Ethernet ADR). test_link_eth pins it, id by id, against literals: a
 * command added here is a decision about what the network may ask for.
 */
#ifndef TFW_LINK_ETH_H
#define TFW_LINK_ETH_H

#include <stdint.h>

#include "tfw/protocol.h"

static inline uint8_t tfw_link_eth_allows(uint8_t cmd)
{
    switch (cmd) {
    case TFW_CMD_CAPS:          /* enrolment: what the part can hold */
    case TFW_CMD_APP_INFO:      /* enrolment: the device profile it runs under */
    case TFW_CMD_ATTEST:        /* UCCS evidence over the verifier's nonce */
    case TFW_CMD_ATTEST_CERT:   /* the alias certificate over the nonce */
    case TFW_CMD_ATTEST_CSR:    /* the templated CSR for ACME finalize */
    case TFW_CMD_OTA_BEGIN:     /* staging into the external store -- ADR */
    case TFW_CMD_OTA_DATA:      /* decision 8's widening, owner answer 8; an */
    case TFW_CMD_OTA_END:       /* image without the stager answers these */
    case TFW_CMD_OTA_STATUS:    /* UNKNOWN_CMD as it always did */
        return 1u;
    default:
        return 0u;
    }
}

#endif /* TFW_LINK_ETH_H */
