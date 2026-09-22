/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE WIRE VECTORS.
 *
 * These bytes are the contract between the C firmware and the Go host SDK.
 * Both assert against them, and the two co-merge only when both are green.
 *
 * They are LITERALS ON PURPOSE. A vector computed from the code it is meant to
 * pin moves whenever the code moves, and would agree with a regression as
 * readily as with the contract. Every value below was produced once, checked
 * against an independent implementation, and then frozen.
 *
 * A FAILURE HERE IS A WIRE BREAK, NOT A TEST TO UPDATE. Changing any of these
 * values makes every already-deployed device fail to talk to an updated host.
 * Changing the contract means a new protocol major version, not new literals.
 *
 * The Go mirror is sdk/go/internal/vectors/vectors.go and CI checks that the
 * two files carry identical values.
 */
#ifndef AG_TESTDATA_VECTORS_H
#define AG_TESTDATA_VECTORS_H

#include <stdint.h>

/* ---------------------------------------------------------------- CRC-16 */
/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR. */

/* The published check value for this polynomial and initial value: the CRC of
 * the nine ASCII bytes "123456789". Every CRC-16/CCITT-FALSE implementation in
 * the world agrees on this number, which is what makes it worth pinning. */
#define AGV_CRC_CHECK_INPUT   "123456789"
#define AGV_CRC_CHECK_LEN     9u
#define AGV_CRC_CHECK_RESULT  0x29B1u

/* The empty input. Folding nothing must leave the initial value untouched --
 * an implementation that special-cases length zero usually gets this wrong. */
#define AGV_CRC_EMPTY_RESULT  0xFFFFu

/* A single zero byte. Distinguishes "init returned unchanged" from "one byte
 * processed", which the empty case alone cannot. */
#define AGV_CRC_ZERO_RESULT   0xE1F0u

/* Two zero bytes, to catch a shift that drops the high half on the second
 * round rather than the first. */
#define AGV_CRC_ZEROZERO_RESULT 0x1D0Fu

/* 0xFF, the all-ones byte, which exercises every branch of the bitwise loop
 * on its first fold. */
#define AGV_CRC_FF_RESULT     0xFF00u

/* ORDER SENSITIVITY. These two inputs are byte-permutations of each other, so
 * an implementation that sums or XORs bytes instead of folding them through
 * the polynomial produces the same value for both. The real-path tests cannot
 * catch that on their own -- both ends would call the same broken function and
 * agree -- so the vector set carries the discriminator explicitly, for the
 * benefit of whoever writes the next implementation. */
#define AGV_CRC_ORDER_A       "\x01\x02"
#define AGV_CRC_ORDER_A_RESULT 0x0E7Cu
#define AGV_CRC_ORDER_B       "\x02\x01"
#define AGV_CRC_ORDER_B_RESULT 0x6B4Cu

#endif /* AG_TESTDATA_VECTORS_H */
