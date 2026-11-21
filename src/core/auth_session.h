/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE OPERATOR-SIGNATURE AUTH SESSION -- issue #4.
 *
 * AUTH_CHALLENGE (0x61) asks the device for a fresh, single-use nonce.
 * AUTH_RESPONSE (0x62) carries a P-256 signature over
 * domain||device_serial||nonce, checked against the operator public key
 * tfwprov baked into the platform-reserved EEPROM block -- never a key held on
 * the device, and never the device's own DeviceID/CDI. A session established
 * this way gates two things: EEPROM_READ below TFW_EEPROM_RESERVED_END, and
 * RESET(1), which makes tfw-boot open an update session (issue #28). RESET(0) is
 * never gated. The session lasts until the device resets;
 * there is no separate inactivity timeout.
 *
 * ELEMENT TIER ONLY. The obvious software-tier primitive, ed25519-donna, does
 * not fit this device family -- see docs/security/threat-model.md and
 * changelog.d/ for the measurement. auth_session.c (this contract's real
 * implementation) links on CRYPTO=atecc608a and auto, on optiga, and on an
 * architecture tier whose board turns AUTH_SESSION on (mk/firmware.mk
 * AUTH_SRCS); auth_session_none.c links everywhere else and answers every
 * gate check permissively, which is why tfw_auth_gate() below is safe to
 * call unconditionally from storage.c without a single #if in either.
 */
#ifndef TFW_CORE_AUTH_SESSION_H
#define TFW_CORE_AUTH_SESSION_H

#include "tfw/status.h"
#include "dispatch.h"

/* Clears any session state. Called once at startup (main.c) and, on the
 * ESP32, again from hal_uart.c's drop_peer() whenever the TCP peer changes.
 * RESET restarts the whole part, which re-runs this the same way. */
void tfw_auth_session_init(void);

/* TFW_OK if a command gated on the session may proceed, TFW_ERR_AUTH_REQUIRED
 * otherwise. The permissive stub (auth_session_none.c) always returns TFW_OK:
 * see docs/security/threat-model.md for why that gap on the soft/none tiers
 * is accepted rather than hidden. */
tfw_status_t tfw_auth_gate(void);

/* AUTH_CHALLENGE and AUTH_RESPONSE, wired into commands.def only when
 * TFW_FEATURE_AUTH_SESSION is on (mk/firmware.mk's AUTH_DEFS). */
tfw_status_t tfw_auth_cmd_challenge(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_auth_cmd_response(tfw_reader_t *r, tfw_writer_t *w);

#endif /* TFW_CORE_AUTH_SESSION_H */
