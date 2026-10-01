/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The one non-elidable wipe, used everywhere a consumed CDI copy is cleared.
 *
 * A plain `memset` on a local buffer whose only remaining use is going out of
 * scope is a DEAD STORE, and an optimizer is free to remove it -- confirmed on
 * this codebase's own `-Os` build (see the verification this file's .c
 * carries). The secret would then survive on the stack for as long as nothing
 * overwrites that memory, defeating the "drain it even with no use for it"
 * rule every CDI-copy site in this tree follows. `ag_secure_zero` writes
 * through a `volatile`-qualified pointer, which the C standard treats as an
 * observable side effect the compiler must not remove, regardless of whether
 * anything reads the memory afterward.
 *
 * One definition, referenced by every CDI-copy site (src/core/handoff.c,
 * boot/boot_main.c, and each of the three crypto providers under
 * src/crypto/) rather than a volatile loop written out again at each one --
 * the same reasoning this project applies to the CRC and the reset-cause
 * decoder.
 */
#ifndef AG_CORE_SECURE_ZERO_H
#define AG_CORE_SECURE_ZERO_H

#include <stddef.h>

void ag_secure_zero(void *p, size_t n);

#endif /* AG_CORE_SECURE_ZERO_H */
