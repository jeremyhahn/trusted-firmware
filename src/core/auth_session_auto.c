/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * CRYPTO=auto's auth session (issue #130): the operator-signature gate of
 * auth_session.c when the dispatch chose the element, auth_session_none.c's
 * permissive answer when it chose the soft root -- the gate each tier's own
 * image has. mk/firmware.mk compiles auth_session.c with AUTH_RENAME, so its
 * init and gate are tfw_auth_element_*; the two commands keep their names.
 * Decided on tfw_crypto_tier(), which src/crypto/auto/provider.c fixes at init.
 */
#include "tfw/crypto.h"
#include "auth_session.h"

void         tfw_auth_element_session_init(void);
tfw_status_t tfw_auth_element_gate(void);

void tfw_auth_session_init(void)
{
    tfw_auth_element_session_init();
}

tfw_status_t tfw_auth_gate(void)
{
    if (tfw_crypto_tier() == TFW_CRYPTO_SECURE_ELEMENT) {
        return tfw_auth_element_gate();
    }
    return TFW_OK;
}
