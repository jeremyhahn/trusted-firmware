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
 * carries them.
 *
 * They are read from here rather than kept a second time, so the alias
 * certificate and the evidence frame cannot come to disagree about what this
 * device is -- a verifier holding both and seeing two different DeviceIDs would
 * be right to reject them both.
 *
 * ZERO until the trusted bootloader supplies them, and deliberately so: a
 * verifier can see an unprovisioned device for what it is, where a
 * plausible-looking made-up value would be believed.
 */
const uint8_t *ag_attest_device_id(void);   /* AG_ATTEST_ID_LEN bytes */
const uint8_t *ag_attest_fw_hash(void);     /* AG_SHA256_LEN bytes    */
#endif
