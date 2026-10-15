/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Which Ethernet controller is on the other end of the SPI chip-select -- a
 * PROBE, not the driver (#55). It runs once on a bench, prints what it read and
 * is thrown away; the W5100/W5500 drivers are tf-55-ethernet's. They may take
 * the detection over, which is why it is split from the reporting in main.c and
 * speaks only through tfw_hal_spi_xfer.
 *
 * Every byte clocked back is kept, not only the one a verdict is drawn from:
 * a chip that answers in a framing it should not is the thing worth seeing.
 */
#ifndef WIZPROBE_H
#define WIZPROBE_H

#include <stdint.h>
#include "tfw/hal.h"

typedef enum {
    WIZPROBE_NONE     = 0,  /* nothing answered in any framing */
    WIZPROBE_W5100    = 1,
    WIZPROBE_W5200    = 2,
    WIZPROBE_W5500    = 3,
    WIZPROBE_ENC28J60 = 4,
    WIZPROBE_UNKNOWN  = 5   /* MISO moves, but matches no pattern below */
} wizprobe_chip_t;

typedef struct {
    /* W5500: 00 39 00 00 -- VERSIONR, common block, read. 0x04 in [3]. */
    uint8_t w5500_rx[4];
    /* W5200: 00 1F 00 01 00 -- VERSIONR, read, length 1. 0x03 in [4]. */
    uint8_t w5200_rx[5];
    /* W5100, the MR/RTR pattern. Each *_rx is the whole 4-byte frame clocked
     * back; a W5100 answers 00 01 02 in the first three bytes of every frame,
     * and the fourth is the register. */
    uint8_t w5100_reset_rx[4];   /* F0 00 00 80: MR <- RST */
    uint8_t w5100_mr;            /* MR after the reset has self-cleared: 0x00 */
    uint8_t w5100_rtr[2];        /* RTR after reset: 07 D0 */
    uint8_t w5100_rcr;           /* RCR after reset: 08 */
    uint8_t w5100_rtr_rx[4];     /* the RTR high-byte read frame, raw */
    uint8_t w5100_rtr_wb[2];     /* RTR after writing 0B B8 */
    /* ENC28J60, only when no WIZnet pattern matched: EREVID (bank 3, 0x12)
     * and ECON1 read back, to show the bank select took. */
    uint8_t enc_tried;
    uint8_t enc_econ1;
    uint8_t enc_erevid;
    wizprobe_chip_t chip;
} wizprobe_result_t;

/* Runs every probe against the part behind cs, on an SPI bus the caller has
 * already brought up (mode 0) with every other chip-select on it deasserted.
 * Leaves a W5100 in its reset state. Needs tfw_hal_millis ticking. */
tfw_status_t wizprobe_run(const tfw_pinref_t *cs, wizprobe_result_t *r);

const char *wizprobe_chip_name(wizprobe_chip_t chip);

#endif /* WIZPROBE_H */
