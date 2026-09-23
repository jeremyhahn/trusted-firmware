/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The one declaration ed25519-donna needs and does not supply, force-included
 * ahead of the vendored translation unit so that not one vendored byte is
 * patched. A patched vendor file would make the hashes in
 * test/third_party/MANIFEST.md pin our edit instead of upstream's code.
 *
 * Upstream's ed25519-randombytes-custom.h is a comment: it documents that the
 * integrator supplies ed25519_randombytes_unsafe and nothing more. Without a
 * declaration ahead of ed25519.c, the reference from the batch-verify path is
 * an implicit declaration, which C99 onwards rejects.
 *
 * THERE IS NO ENTROPY SOURCE HERE AND THIS IS NOT ONE. No AVR in this family
 * has a TRNG -- src/crypto/soft/provider.c refuses ag_crypto_random for exactly
 * this reason rather than returning something that merely looks random. The
 * definition in verify_ed25519_donna.c is reachable only from
 * ed25519_sign_open_batch, which this bootloader never calls and --gc-sections
 * removes from the image. It exists to satisfy the compiler, it produces
 * nothing, and anything that started calling it would be a bug this comment is
 * the evidence for.
 */
#ifndef AG_DONNA_SHIM_H
#define AG_DONNA_SHIM_H

#include <stddef.h>

void ed25519_randombytes_unsafe(void *p, size_t len);

#endif /* AG_DONNA_SHIM_H */
