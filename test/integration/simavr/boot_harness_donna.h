/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The soft-ed25519 leg's signer (#137, CA m522): the one vendored
 * ed25519-donna, built for the host inside the harness's own translation unit.
 *
 * Included ONCE, at the end of boot_harness.c, so that none of donna's macros
 * reach the harness's code. It is a header and not a .c because the harness's
 * build (mk/boot.mk) names its sources one by one, and this leg adds none.
 * The defines and the shim are the boot's own (DONNA_CFLAGS, mk/boot.mk), so
 * the host signs with the code the AVR verifies with. Not one vendored byte is
 * patched; the warnings are silenced for the vendored tree only, as every
 * vendored tree's are.
 */
#ifndef TFW_BOOT_HARNESS_DONNA_H
#define TFW_BOOT_HARNESS_DONNA_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ED25519_REFHASH         /* donna's own SHA-512, as on the AVR */
#define ED25519_CUSTOMRANDOM    /* the randomness below, not OpenSSL's */
#define ED25519_NO_INLINE_ASM   /* portable C, as on the AVR */
#define ED25519_FORCE_32BIT     /* the 32-bit field code, the only one vendored */

#include "verify/donna_shim.h"

#pragma GCC diagnostic push
/* An option this compiler does not know is not a reason to fail. */
#pragma GCC diagnostic ignored "-Wpragmas"
/* donna narrows limbs by design; the arithmetic is upstream's to answer for. */
#pragma GCC diagnostic ignored "-Wconversion"
/* The same narrowing between signed and unsigned limbs. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
/* Loop counters compared across signedness in the vendored field code. */
#pragma GCC diagnostic ignored "-Wsign-compare"
/* Vendored locals reuse names of outer ones. */
#pragma GCC diagnostic ignored "-Wshadow"
/* Vendored helpers this leg never calls (batch verify, curve25519). */
#pragma GCC diagnostic ignored "-Wunused-function"
/* Vendored parameters some configurations leave unread. */
#pragma GCC diagnostic ignored "-Wunused-parameter"
/* Vendored locals some configurations set and never read. */
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
/* Vendored switch fall-throughs carry upstream's comments, not ours. */
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "third_party/ed25519-donna/ed25519.c"
#pragma GCC diagnostic pop

/* Reachable only from ed25519_sign_open_batch, which this harness never calls.
 * Signing is deterministic (RFC 8032) and needs none -- see donna_shim.h. */
void ed25519_randombytes_unsafe(void *p, size_t len)
{
    memset(p, 0, len);
}

#endif /* TFW_BOOT_HARNESS_DONNA_H */
