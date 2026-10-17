/*
 * A one-slot board: its image has no OTA_CONFIRM (src/core/ota_ctl.c needs
 * BOOT_AB) and its boot no confirm request, so the confirm is the CONFIRMED
 * state written into the block, as the trial ADR's state machine stores it.
 * There is no swap record.
 */
#include <stdio.h>

#include "tfw/boot.h"
#include "boot_harness_ab.h"

void harness_confirm(avr_t *avr, uint8_t *ee, uint8_t *block, tfw_boot_platform_t *p)
{
    (void)avr;
    (void)ee;
    p->boot_state = TFW_BOOT_STATE_CONFIRMED;
    tfw_boot_platform_serialise(p, block);
}

void harness_print_swap_record(avr_t *avr)
{
    (void)avr;
    printf("  swap record: none on this board\n");
}
