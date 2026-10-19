/* SPDX-License-Identifier: commercial */
/*
 * The application's half of the one trial (decision 9 of
 * docs/ota/adr-ota-image-format-and-trust.md).
 *
 * tfw-boot arms the watchdog before the trial jump; the application's .init3
 * disarms it, as on every boot, because MCUSR and WDE leave it no other choice.
 * So at start, before the application's init, this reads the platform block's
 * boot_state and, in TRIAL, re-arms the watchdog at the part's longest period.
 * The poll then pets it and counts TFW_OTA_TRIAL_TIMEOUT_S; at expiry it resets
 * into tfw-boot, which finds TRIAL and settles it by OTA_CONFIRM's request or
 * rolls back. Nothing stops the count (#137): a confirm the application cannot
 * check must not keep a trial running.
 *
 * On ESP32 (#48) the trial is ESP-IDF's: the running image PENDING_VERIFY.
 * There OTA_CONFIRM ends it, so at the deadline only an unconfirmed trial
 * resets, and ESP-IDF's bootloader rolls it back.
 *
 * Linked on a board with a boot section and on ESP32; ota_trial_none.c
 * elsewhere. Whether the trial is on is the platform's: ota_trial_block.c reads
 * the platform block, src/hal/esp32/hal_ota.c asks esp_ota.
 */
#ifndef TFW_OTA_TRIAL_H
#define TFW_OTA_TRIAL_H

#include <stdint.h>

/* Reads boot_state; in TRIAL, re-arms the watchdog and starts the deadline.
 * In any other state, or with no valid platform block, arms nothing. */
void tfw_ota_trial_start(void);

/* After the runner's start, once. In TRIAL, asks the runner for
 * tfw_app_after_update's answer (tfw_app_runner_after_update) and resets into
 * tfw-boot on a refusal, so the trial ends at once as failed. TFW_OK, or any
 * other state, changes nothing. */
void tfw_ota_trial_after_init(void);

/* From the superloop: during the trial, pets the watchdog and, once
 * TFW_OTA_TRIAL_TIMEOUT_S has passed, resets, confirmed or not. Never blocks. */
void tfw_ota_trial_poll(void);

/* 1 while the running image is on its trial: boot_state TRIAL in the platform
 * block (ota_trial_block.c), or PENDING_VERIFY in otadata (hal_ota.c). Asked
 * at start and again at the deadline. */
uint8_t tfw_ota_trial_on(void);

#endif /* TFW_OTA_TRIAL_H */
