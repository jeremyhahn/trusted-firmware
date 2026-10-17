/*
 * The 2560 (BOOT_AB): the application confirms with OTA_CONFIRM's request
 * record (OTA ADR decision 8), outside the platform block, which tfw-boot
 * alone writes; tfw-boot confirms and settles at the reset. The swap record
 * sits in front of it.
 */
#include <stdio.h>
#include <string.h>

#include <simavr/avr_eeprom.h>

#include "tfw/config.h"
#include "tfw/boot.h"
#include "crc16.h"
#include "boot_harness_ab.h"

void harness_confirm(avr_t *avr, uint8_t *ee, uint8_t *block, tfw_boot_platform_t *p)
{
    uint8_t *r = &ee[TFW_BOOT_CONFIRM_ADDR];
    uint16_t c;

    tfw_boot_platform_serialise(p, block);
    r[0] = TFW_BOOT_CONFIRM_MAGIC;
    memcpy(&r[TFW_BOOT_CONFIRM_O_MEAS],
           &avr->flash[TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_MEASUREMENT],
           TFW_BOOT_CONFIRM_MEAS_LEN);
    c = tfw_crc16(r, TFW_BOOT_CONFIRM_O_CRC);
    r[TFW_BOOT_CONFIRM_O_CRC]      = (uint8_t)(c & 0xFFu);
    r[TFW_BOOT_CONFIRM_O_CRC + 1u] = (uint8_t)(c >> 8);
}

/* Phase, then unit (boot_swap.c), then the confirm request. */
void harness_print_swap_record(avr_t *avr)
{
    avr_eeprom_desc_t d = { .ee = NULL, .offset = TFW_SWAP_RECORD_ADDR,
                            .size = TFW_BOOT_EE_EXTRA_LEN };
    uint32_t i;

    (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
    printf("  swap record:");
    for (i = 0u; d.ee != NULL && i < TFW_BOOT_EE_EXTRA_LEN; i++) {
        printf(" %02x", d.ee[i]);
    }
    printf("\n");
}
