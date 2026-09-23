/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The one symbol ed25519-donna expects the integrator to define, in its own
 * translation unit so that every link that pulls in the vendored code gets it
 * -- the AVR bootloader, and each host suite that exercises the primitive.
 *
 * THERE IS NO ENTROPY SOURCE HERE AND THIS IS NOT ONE. No AVR in this family
 * has a TRNG, which is why src/crypto/soft/provider.c refuses ag_crypto_random
 * rather than returning something that merely looks random.
 *
 * It is reachable only from ed25519_sign_open_batch, which nothing in this tree
 * calls. On the device --gc-sections removes it entirely; on the host the
 * linker keeps it because the whole object is linked, which is why it must
 * exist at all.
 *
 * It fills with zeros deliberately. A plausible-looking PRNG would be worse,
 * because zeros cannot be mistaken for randomness by whoever reads this next.
 */

#include <string.h>

#include "donna_shim.h"

void ed25519_randombytes_unsafe(void *p, size_t len)
{
    memset(p, 0, len);
}
