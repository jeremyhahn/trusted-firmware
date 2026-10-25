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
    uint8_t       *r = &ee[TFW_BOOT_CONFIRM_ADDR];
    const uint8_t *tr;
    uint16_t       c;

    tfw_boot_platform_serialise(p, block);
    r[0] = TFW_BOOT_CONFIRM_MAGIC;
    memcpy(&r[TFW_BOOT_CONFIRM_O_MEAS],
           &avr->flash[TFW_BOOT_TRAILER_ADDR + TFW_BOOT_TR_MEASUREMENT],
           TFW_BOOT_CONFIRM_MEAS_LEN);
    /* Signed by the row key, as go-xkms signs it (#231): tfw-boot rebuilds
     * the payload from the primary's trailer and verifies it. */
    tr = &avr->flash[TFW_BOOT_TRAILER_ADDR];
    harness_sign_confirm(&tr[TFW_BOOT_TR_MEASUREMENT],
                         (uint32_t)tr[TFW_BOOT_TR_IMAGE_VERSION]
                             | ((uint32_t)tr[TFW_BOOT_TR_IMAGE_VERSION + 1u] << 8)
                             | ((uint32_t)tr[TFW_BOOT_TR_IMAGE_VERSION + 2u] << 16)
                             | ((uint32_t)tr[TFW_BOOT_TR_IMAGE_VERSION + 3u] << 24),
                         &r[TFW_BOOT_CONFIRM_O_SIG]);
    c = tfw_crc16(r, TFW_BOOT_CONFIRM_O_CRC);
    r[TFW_BOOT_CONFIRM_O_CRC]      = (uint8_t)(c & 0xFFu);
    r[TFW_BOOT_CONFIRM_O_CRC + 1u] = (uint8_t)(c >> 8);
}

/* Phase, then unit (boot_swap.c), then the confirm request. */
void harness_print_swap_record(avr_t *avr)
{
    /* From the journal through the confirm request's last byte. */
    const uint32_t    len = TFW_BOOT_CONFIRM_ADDR + TFW_BOOT_CONFIRM_LEN
                             - TFW_SWAP_RECORD_ADDR;
    avr_eeprom_desc_t d = { .ee = NULL, .offset = TFW_SWAP_RECORD_ADDR,
                            .size = len };
    uint32_t i;

    (void)avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
    printf("  swap record:");
    for (i = 0u; d.ee != NULL && i < len; i++) {
        printf(" %02x", d.ee[i]);
    }
    printf("\n");
}
