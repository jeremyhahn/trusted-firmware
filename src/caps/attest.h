/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_ATTEST_H
#define TFW_CAPS_ATTEST_H
#include "tfw/crypto.h"
#include "tfw/status.h"
#include "../core/reader.h"
#include "../core/writer.h"
tfw_status_t tfw_attest_cmd(tfw_reader_t *r, tfw_writer_t *w);
void        tfw_attest_reset_state(void);   /* test seam */

/*
 * The device identity and the firmware measurement, as the evidence frame
 * also carries them.
 *
 * These read their startup sources DIRECTLY -- src/core/platform_id.c and
 * src/core/handoff.c -- not a copy kept inside the evidence buffer, and not
 * only after ATTEST has run: src/caps/alias.c's mint() calls these before it
 * has any guarantee that ATTEST ever has, and a certificate signed over a
 * stale (zero) UEID on a fully provisioned device would be indistinguishable
 * from a genuine unprovisioned one. Reading one shared source is also what
 * keeps the alias certificate and the evidence frame from disagreeing with
 * each other about what this device is.
 *
 * DeviceID comes from the platform-reserved EEPROM block (tfwprov bakes it in
 * before flashing); FWHash comes from the tfw-boot CDI handoff. Both are ZERO
 * wherever their source is absent -- an unprovisioned platform block, a stock
 * bootloader, or no bootloader at all -- rather than fabricated: a verifier
 * can see an unprovisioned device for what it is, where a plausible-looking
 * made-up value would be believed.
 */
const uint8_t *tfw_attest_device_id(void);   /* TFW_ATTEST_ID_LEN bytes */
const uint8_t *tfw_attest_fw_hash(void);     /* TFW_SHA256_LEN bytes    */

/*
 * The 32 bytes the measurements claim (273) carries as the hash-entry value of
 * its one file, "app": the application's evidence when an application is
 * running and supplies it (tfw_app_evidence, include/tfw/app.h), otherwise the
 * firmware measurement above, exactly as before the application had a say. The
 * claim is go-xkms's shape -- one file, one 32-byte value -- so it has room for
 * one or the other, never both, and never zeros standing in for an
 * application's bytes: an application that supplies none leaves the firmware
 * measurement where it was.
 */
void tfw_attest_measurement(uint8_t out[TFW_SHA256_LEN]);
#endif
