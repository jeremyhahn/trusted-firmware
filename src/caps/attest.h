/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_ATTEST_H
#define AG_CAPS_ATTEST_H
#include "ag/status.h"
#include "../core/reader.h"
#include "../core/writer.h"
ag_status_t ag_attest_cmd(ag_reader_t *r, ag_writer_t *w);
void        ag_attest_reset_state(void);   /* test seam */

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
 * DeviceID comes from the platform-reserved EEPROM block (agprov bakes it in
 * before flashing); FWHash comes from the ag-boot CDI handoff. Both are ZERO
 * wherever their source is absent -- an unprovisioned platform block, a stock
 * bootloader, or no bootloader at all -- rather than fabricated: a verifier
 * can see an unprovisioned device for what it is, where a plausible-looking
 * made-up value would be believed.
 */
const uint8_t *ag_attest_device_id(void);   /* AG_ATTEST_ID_LEN bytes */
const uint8_t *ag_attest_fw_hash(void);     /* AG_SHA256_LEN bytes    */
#endif
