/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE OPERATOR-SIGNATURE AUTH SESSION -- issue #4.
 *
 * AUTH_CHALLENGE (0x61) asks the device for a fresh, single-use nonce.
 * AUTH_RESPONSE (0x62) carries a P-256 signature over
 * domain||device_serial||nonce, checked against the operator public key
 * agprov baked into the platform-reserved EEPROM block -- never a key held on
 * the device, and never the device's own DeviceID/CDI. A session established
 * this way gates two things: EEPROM_READ below AG_EEPROM_RESERVED_END, and
 * RESET(1), which makes ag-boot open an update session (issue #28). RESET(0) is
 * never gated. The session lasts until the device resets;
 * there is no separate inactivity timeout.
 *
 * ELEMENT TIER ONLY. The obvious software-tier primitive, ed25519-donna, does
 * not fit this device family -- see docs/security/threat-model.md and
 * changelog.d/ for the measurement. auth_session.c (this contract's real
 * implementation) links only on CRYPTO=atecc608a; auth_session_none.c links
 * everywhere else and answers every gate check permissively, which is why
 * ag_auth_gate() below is safe to call unconditionally from storage.c
 * without a single #if in either.
 */
#ifndef AG_CORE_AUTH_SESSION_H
#define AG_CORE_AUTH_SESSION_H

#include "ag/status.h"
#include "dispatch.h"

/* Clears any session state. Called once at startup and nowhere else in this
 * build -- RESET restarts the whole part, which re-runs this the same way. */
void ag_auth_session_init(void);

/* AG_OK if a command gated on the session may proceed, AG_ERR_AUTH_REQUIRED
 * otherwise. The permissive stub (auth_session_none.c) always returns AG_OK:
 * see docs/security/threat-model.md for why that gap on the soft/none tiers
 * is accepted rather than hidden. */
ag_status_t ag_auth_gate(void);

/* AUTH_CHALLENGE and AUTH_RESPONSE, wired into commands.def only when
 * AG_FEATURE_AUTH_SESSION is on (mk/avr.mk, CRYPTO=atecc608a). */
ag_status_t ag_auth_cmd_challenge(ag_reader_t *r, ag_writer_t *w);
ag_status_t ag_auth_cmd_response(ag_reader_t *r, ag_writer_t *w);

#endif /* AG_CORE_AUTH_SESSION_H */
