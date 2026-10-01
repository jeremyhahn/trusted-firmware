/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The no-session stub, linked instead of auth_session.c on the soft and
 * CRYPTO=none tiers -- issue #4's owner ruling. AUTH_CHALLENGE/AUTH_RESPONSE
 * are not compiled into either tier at all (mk/avr.mk leaves them out of
 * commands.def), so ag_auth_cmd_challenge/_response are never referenced and
 * --gc-sections drops them from this file's object entirely if they were
 * ever pulled in; only ag_auth_session_init and ag_auth_gate are called
 * unconditionally, from main.c and from storage.c respectively, which
 * is why those two stay real symbols here.
 *
 * ag_auth_gate() answers AG_OK always. That is not an oversight: the owner
 * ruling for issue #4 is that reserved-EEPROM reads are NOT gated on these tiers, because the only element-free primitive that
 * could gate them (ed25519-donna) does not fit this device family -- see
 * docs/security/threat-model.md for the accepted-gap statement and
 * changelog.d/ for the measurement that ruled it out.
 */
#include "auth_session.h"

void ag_auth_session_init(void) {}

ag_status_t ag_auth_gate(void)
{
    return AG_OK;
}
