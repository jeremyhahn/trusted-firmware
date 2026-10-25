/*
 * What the boot harness does differently on a board with two slots (BOOT_AB),
 * selected by the object list in mk/boot.mk: boot_harness_ab.c on the 2560,
 * boot_harness_ab_none.c on a one-slot board. No #if in the harness.
 */
#ifndef TFW_BOOT_HARNESS_AB_H
#define TFW_BOOT_HARNESS_AB_H

#include <stdint.h>

#include <simavr/sim_avr.h>

#include "tfw/boot.h"
#include "boot_platform.h"

/* The application confirms its trial, as the board's image does, for the reset
 * that follows. Call after carry(): ee is the EEPROM it took, block the
 * platform block install() writes over it, p the block as stored. */
void harness_confirm(avr_t *avr, uint8_t *ee, uint8_t *block, tfw_boot_platform_t *p);

/* The row key's signature over a confirm of the image measuring `hash` at
 * `version` (#231), from boot_harness.c, which holds the key. */
void harness_sign_confirm(const uint8_t hash[32], uint32_t version,
                          uint8_t sig[TFW_BOOT_CONFIRM_SIG_LEN]);

/* The swap record as tfw-boot last wrote it, for a failure's log. */
void harness_print_swap_record(avr_t *avr);

#endif
