/*
 * boot/hal/host/boot_host.h -- driving tfw-boot inside a host process (#52).
 *
 * boot_hal_host.c is the boot HAL for tfwsim: the EEPROM and the handoff are
 * the mock HAL's (test/mock/hal_mock.c), so tfw-boot and the firmware share one
 * of each as they do on the part; the external store is the firmware stager's
 * own SPI NOR model; the flash is a buffer here. The three ways out of a boot
 * pass -- the jump, a reset, recovery -- return to the caller of
 * tfw_boot_host_pass instead of leaving the process.
 */
#ifndef TFW_BOOT_HOST_H
#define TFW_BOOT_HOST_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/types.h"

typedef enum {
    TFW_BOOT_HOST_JUMPED   = 1,  /* handed over: the application runs next */
    TFW_BOOT_HOST_RESET    = 2,  /* tfw_boot_reset: run the pass again */
    TFW_BOOT_HOST_RECOVERY = 3   /* tfw_boot_recovery: see the reason */
} tfw_boot_host_exit_t;

/* The flash, TFW_FLASH_TOTAL bytes, erased (0xFF) until written. */
uint8_t *tfw_boot_host_flash(void);

/* The cause the next pass reads from tfw_boot_reset_cause. */
void tfw_boot_host_set_cause(tfw_reset_cause_t c);

/* One pass of tfw-boot's entry point, to whichever exit it takes. */
tfw_boot_host_exit_t tfw_boot_host_pass(int (*entry)(void));

/* The status tfw_boot_recovery was entered with, after a RECOVERY exit. */
tfw_status_t tfw_boot_host_recovery_reason(void);

#endif /* TFW_BOOT_HOST_H */
