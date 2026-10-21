/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Where a CDI could still be after the code that made it has returned: the
 * stack below the caller, and the buffers tfw_secure_zero was handed.
 *
 * A suite that links cdi_residue.c links it IN PLACE of src/core/secure_zero.c:
 * the tfw_secure_zero here zeroes the same way, through a volatile pointer, and
 * also records what each buffer held before and whether it read back zero. So a
 * suite can say which clear took the secret, and a clear removed by hand shows
 * as a record that never held it.
 *
 * The stack is read by tfw_residue_snapshot(), which copies the bytes under its
 * own frame -- where the frames of whatever the caller called last were. Call
 * it straight after the call under test, from the same function, before any
 * assertion: anything called in between writes over what it would have found.
 */
#ifndef TFW_TEST_CDI_RESIDUE_H
#define TFW_TEST_CDI_RESIDUE_H

#include <stddef.h>
#include <stdint.h>

/* Forget every recorded clear. */
void tfw_residue_reset(void);

/* How many times tfw_secure_zero was called since the reset. */
unsigned tfw_residue_zero_calls(void);

/* Nonzero if a recorded clear held any 16-byte part of needle (n a multiple
 * of 16) before it ran and read back all zero after. */
int tfw_residue_cleared(const void *needle, size_t n);

/* Nonzero if the clear `back` calls before the last (0: the last) was of
 * exactly len bytes, held a 16-byte part of needle and read back all zero. */
int tfw_residue_last_cleared(unsigned back, size_t len, const void *needle, size_t n);

/* Copy the stack under the caller's frame for tfw_residue_on_stack(). */
void tfw_residue_snapshot(void);

/* Nonzero if the last snapshot holds any 16-byte part of needle (n a multiple
 * of 16). */
int tfw_residue_on_stack(const void *needle, size_t n);

/* Nonzero if buf holds any 16-byte part of needle (n a multiple of 16). */
int tfw_residue_in(const void *buf, size_t len, const void *needle, size_t n);

#endif /* TFW_TEST_CDI_RESIDUE_H */
