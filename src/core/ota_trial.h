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
 * into tfw-boot, which finds TRIAL and rolls back.
 *
 * Linked on a board with a boot section; ota_trial_none.c elsewhere.
 */
#ifndef TFW_OTA_TRIAL_H
#define TFW_OTA_TRIAL_H

/* Reads boot_state; in TRIAL, re-arms the watchdog and starts the deadline.
 * In any other state, or with no valid platform block, arms nothing. */
void tfw_ota_trial_start(void);

/* From the superloop: during the trial, pets the watchdog and, once
 * TFW_OTA_TRIAL_TIMEOUT_S has passed unconfirmed, resets. Never blocks. */
void tfw_ota_trial_poll(void);

/* Stops the count: the call the OTA_CONFIRM handler makes once the confirm
 * is on the wire (decision 8, PROPOSED). The watchdog stays armed until the
 * next reset, so the poll keeps petting it. */
void tfw_ota_trial_stop(void);

#endif /* TFW_OTA_TRIAL_H */
